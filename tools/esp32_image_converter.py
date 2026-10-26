"""ESP32-P4 image converter: Korean desktop UI and batch CLI (Pillow required)."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import io
import os
from pathlib import Path
import queue
import threading
import warnings

from PIL import Image, ImageOps

EXTENSIONS = {'.jpg', '.jpeg', '.png', '.bmp', '.webp', '.gif', '.tif', '.tiff', '.ico'}
PROFILES = {
    'p4-photo': ('P4 / M3 사진 앱 · JPG 432×480 (권장)', 'jpeg', 432, 480),
    'p4-screen': ('P4 세로 화면 · JPG 480×800', 'jpeg', 480, 800),
    'p4-bmp': ('P4 / M3 사진 앱 · BMP 24비트 432×480', 'bmp', 432, 480),
    'web-small': ('웹페이지용 · WebP 800×800 (사진 앱 사용 불가)', 'webp', 800, 800),
}
SUFFIXES = {'jpeg': '.jpg', 'bmp': '.bmp', 'webp': '.webp'}
MODES = {'fit': '비율 유지 · 최대 크기 안에 맞춤',
         'pad': '전체 보존 · 검정 여백 추가',
         'crop': '화면 채우기 · 중앙 자르기'}


@dataclass(frozen=True)
class Options:
    format: str = 'jpeg'
    width: int = 432
    height: int = 480
    quality: int = 80
    mode: str = 'fit'

    def validate(self):
        if self.format not in SUFFIXES or self.mode not in MODES:
            raise ValueError('지원하지 않는 형식 또는 크기 조절 방식입니다.')
        if not (1 <= self.width <= 2048 and 1 <= self.height <= 2048
                and self.width * self.height <= 2 * 1024 * 1024):
            raise ValueError('가로·세로는 1~2048, 전체 픽셀은 2,097,152 이하여야 합니다.')
        if not 1 <= self.quality <= 95:
            raise ValueError('품질은 1~95 사이로 지정하세요.')


def encode(path: Path, options: Options) -> tuple[bytes, tuple[int, int]]:
    options.validate()
    with warnings.catch_warnings():
        warnings.simplefilter('error', Image.DecompressionBombWarning)
        with Image.open(path) as source:
            # Only the first frame/page is exported; metadata is deliberately omitted.
            image = ImageOps.exif_transpose(source).convert('RGBA')
    rgb = Image.new('RGB', image.size, 'black')
    rgb.paste(image, mask=image.getchannel('A'))
    size = (options.width, options.height)
    if options.mode == 'crop':
        rgb = ImageOps.fit(rgb, size, method=Image.Resampling.LANCZOS)
    else:
        rgb.thumbnail(size, Image.Resampling.LANCZOS)
        if options.mode == 'pad':
            canvas = Image.new('RGB', size, 'black')
            canvas.paste(rgb, ((size[0] - rgb.width) // 2, (size[1] - rgb.height) // 2))
            rgb = canvas
    buffer = io.BytesIO()
    if options.format == 'jpeg':
        rgb.save(buffer, 'JPEG', quality=options.quality, subsampling=2,
                 progressive=False, optimize=False)
    elif options.format == 'bmp':
        rgb.save(buffer, 'BMP')  # RGB produces uncompressed 24-bit BI_RGB.
    else:
        rgb.save(buffer, 'WEBP', quality=options.quality, method=4)
    data = buffer.getvalue()
    if len(data) > 4 * 1024 * 1024:
        raise ValueError('결과가 사진 앱의 4 MiB 제한을 넘습니다. 크기를 줄여 주세요.')
    return data, rgb.size


def convert(source: Path, output: Path, options: Options, recursive=True,
            cancel=None, emit=lambda message: None) -> dict:
    options.validate()
    source, output = source.resolve(), output.resolve()
    if not source.is_dir() and not source.is_file():
        raise ValueError('입력 파일 또는 폴더를 찾을 수 없습니다.')
    base = source if source.is_dir() else source.parent
    if output == base or base.is_relative_to(output):
        raise ValueError('저장 폴더는 입력 폴더 및 그 상위 폴더와 달라야 합니다.')
    cancel = cancel or threading.Event()
    if source.is_file():
        files = [source]
    else:
        files = []
        for directory, dirs, names in os.walk(source, followlinks=False):
            dirs[:] = sorted(d for d in dirs if not (Path(directory) / d).is_symlink()
                             and not (Path(directory) / d).resolve().is_relative_to(output)) if recursive else []
            files.extend(Path(directory) / n for n in sorted(names)
                         if Path(n).suffix.lower() in EXTENSIONS and not (Path(directory) / n).is_symlink())
    stats = dict(converted=0, skipped=0, failed=0, before=0, after=0, cancelled=False)
    reserved = set()
    emit(f'이미지 {len(files)}개 · {options.width}×{options.height} · {options.format.upper()}')
    for index, path in enumerate(files, 1):
        if cancel.is_set():
            stats['cancelled'] = True
            break
        relative = path.relative_to(base)
        target = output / relative.with_suffix(SUFFIXES[options.format])
        number = 1
        while str(target).casefold() in reserved:
            number += 1
            target = output / relative.parent / f'{relative.stem}_{number}{SUFFIXES[options.format]}'
        reserved.add(str(target).casefold())
        if target.exists():
            stats['skipped'] += 1
            emit(f'[{index}/{len(files)}] 기존 파일 보존: {target.relative_to(output)}')
            continue
        try:
            data, size = encode(path, options)
            if cancel.is_set():
                stats['cancelled'] = True
                break
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                with target.open('xb') as dest:
                    try:
                        dest.write(data)
                    except BaseException:
                        dest.close()
                        target.unlink(missing_ok=True)
                        raise
            except FileExistsError:
                stats['skipped'] += 1
                continue
            stats['converted'] += 1
            stats['before'] += path.stat().st_size
            stats['after'] += len(data)
            emit(f'[{index}/{len(files)}] {target.relative_to(output)} · {size[0]}×{size[1]} · {len(data)/1024:.1f} KiB')
        except Exception as exc:
            stats['failed'] += 1
            emit(f'실패: {relative}: {exc}')
    emit(f"{'중단' if stats['cancelled'] else '완료'}: 성공 {stats['converted']}, "
         f"건너뜀 {stats['skipped']}, 실패 {stats['failed']} · "
         f"성공 파일 {stats['before']/1048576:.2f} → {stats['after']/1048576:.2f} MiB")
    return stats


def launch_gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText

    root = tk.Tk()
    root.title('ESP32-P4 이미지 변환기')
    root.geometry('860x620')
    root.minsize(720, 520)
    root.columnconfigure(1, weight=1)
    root.rowconfigure(8, weight=1)
    source, output = tk.StringVar(), tk.StringVar()
    profile = tk.StringVar(value=PROFILES['p4-photo'][0])
    fmt, width, height = tk.StringVar(value='jpeg'), tk.StringVar(value='432'), tk.StringVar(value='480')
    quality, mode = tk.StringVar(value='80'), tk.StringVar(value=MODES['fit'])
    recursive = tk.BooleanVar(value=True)
    events, cancel = queue.Queue(), threading.Event()
    busy = False
    controls = []

    def choose(file=False):
        selected = (filedialog.askopenfilename(title='변환할 이미지', filetypes=[('이미지', ' '.join('*'+e for e in sorted(EXTENSIONS))), ('모든 파일', '*.*')])
                    if file else filedialog.askdirectory(title='이미지 폴더'))
        if selected:
            source.set(selected)
            path = Path(selected)
            output.set(str((path.parent if file else path) / 'esp32_images'))

    for row, label, variable in [(0, '입력 파일 / 폴더', source), (1, '저장 폴더', output)]:
        ttk.Label(root, text=label).grid(row=row, column=0, padx=12, pady=8, sticky='w')
        entry = ttk.Entry(root, textvariable=variable)
        entry.grid(row=row, column=1, sticky='ew')
        controls.append(entry)
    box = ttk.Frame(root)
    box.grid(row=0, column=2, padx=10)
    for label, callback in [('파일', lambda: choose(True)), ('폴더', choose)]:
        button = ttk.Button(box, text=label, command=callback)
        button.pack(side='left')
        controls.append(button)
    def choose_output():
        selected = filedialog.askdirectory(title='저장 폴더')
        if selected:
            output.set(selected)
    button = ttk.Button(root, text='찾아보기', command=choose_output)
    button.grid(row=1, column=2)
    controls.append(button)
    def combo(row, label, variable, values):
        ttk.Label(root, text=label).grid(row=row, column=0, padx=12, pady=8, sticky='w')
        widget = ttk.Combobox(root, textvariable=variable, values=values, state='readonly')
        widget.grid(row=row, column=1, columnspan=2, padx=(0, 12), sticky='ew')
        controls.append(widget)
        return widget
    preset = combo(2, '빠른 설정', profile, [p[0] for p in PROFILES.values()])
    def apply_profile(_event=None):
        _, format_value, w, h = next(p for p in PROFILES.values() if p[0] == profile.get())
        fmt.set(format_value); width.set(str(w)); height.set(str(h))
    preset.bind('<<ComboboxSelected>>', apply_profile)
    combo(3, '저장 형식', fmt, list(SUFFIXES))
    dimensions = ttk.Frame(root)
    dimensions.grid(row=4, column=1, columnspan=2, sticky='w')
    ttk.Label(root, text='크기 / 품질').grid(row=4, column=0, padx=12, sticky='w')
    for label, variable in [('가로', width), ('세로', height), ('품질 1~95', quality)]:
        ttk.Label(dimensions, text=label).pack(side='left', padx=5)
        entry = ttk.Entry(dimensions, width=7, textvariable=variable)
        entry.pack(side='left')
        controls.append(entry)
    combo(5, '크기 조절', mode, list(MODES.values()))
    check = ttk.Checkbutton(root, text='하위 폴더 포함 (폴더 구조 유지)', variable=recursive)
    check.grid(row=6, column=1, sticky='w', pady=8)
    controls.append(check)
    ttk.Label(root, text='JPG: RGB · 비진행형 · 4:2:0 / BMP: 무압축 24비트 / WebP: 웹 전용\n투명 영역은 검정으로 처리합니다. GIF·TIFF는 첫 화면만 변환합니다. 원본·기존 결과는 보존합니다.',
              wraplength=800).grid(row=7, column=0, columnspan=3, padx=12, pady=8, sticky='w')
    log = ScrolledText(root, height=12, state='disabled')
    log.grid(row=8, column=0, columnspan=3, padx=12, pady=5, sticky='nsew')
    actions = ttk.Frame(root)
    actions.grid(row=9, column=0, columnspan=3, pady=10)
    progress = ttk.Progressbar(root, mode='indeterminate')
    progress.grid(row=10, column=0, columnspan=3, sticky='ew', padx=12, pady=(0, 10))

    def start():
        nonlocal busy
        try:
            if not source.get().strip() or not output.get().strip():
                raise ValueError('입력과 저장 위치를 선택하세요.')
            options = Options(fmt.get(), int(width.get()), int(height.get()), int(quality.get()),
                              next(k for k, v in MODES.items() if v == mode.get()))
            options.validate()
            src, dst, recurse = Path(source.get()), Path(output.get()), recursive.get()
        except (ValueError, StopIteration) as exc:
            messagebox.showerror('설정 확인', str(exc)); return
        busy = True
        cancel.clear()
        for widget in controls:
            widget.configure(state='disabled')
        start_button.configure(state='disabled')
        stop_button.configure(state='normal')
        progress.start()
        def worker():
            try:
                convert(src, dst, options, recurse, cancel, lambda text: events.put(('log', text)))
            except Exception as exc:
                events.put(('log', f'오류: {exc}'))
            finally:
                events.put(('done', None))
        threading.Thread(target=worker, daemon=True).start()

    start_button = ttk.Button(actions, text='변환 시작', command=start)
    start_button.pack(side='left', padx=6)
    stop_button = ttk.Button(actions, text='중단', command=cancel.set, state='disabled')
    stop_button.pack(side='left', padx=6)
    def poll():
        nonlocal busy
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == 'log':
                    log.configure(state='normal'); log.insert('end', value+'\n'); log.see('end'); log.configure(state='disabled')
                else:
                    busy = False
                    progress.stop()
                    start_button.configure(state='normal'); stop_button.configure(state='disabled')
                    for widget in controls:
                        widget.configure(state='readonly' if isinstance(widget, ttk.Combobox) else 'normal')
        except queue.Empty:
            pass
        root.after(100, poll)
    def close():
        if busy:
            cancel.set()
            events.put(('log', '중단 요청됨. 변환이 끝나면 창을 닫아 주세요.'))
        else:
            root.destroy()
    root.protocol('WM_DELETE_WINDOW', close)
    poll()
    root.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?', type=Path)
    parser.add_argument('-o', '--output', type=Path)
    parser.add_argument('--profile', choices=PROFILES, default='p4-photo')
    parser.add_argument('--format', choices=SUFFIXES)
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    parser.add_argument('--quality', type=int, default=80)
    parser.add_argument('--mode', choices=MODES, default='fit')
    parser.add_argument('--no-recursive', action='store_true')
    args = parser.parse_args()
    if args.source is None:
        launch_gui(); return 0
    _, fmt, width, height = PROFILES[args.profile]
    options = Options(args.format or fmt, args.width if args.width is not None else width,
                      args.height if args.height is not None else height, args.quality, args.mode)
    output = args.output or ((args.source if args.source.is_dir() else args.source.parent) / 'esp32_images')
    try:
        result = convert(args.source, output, options, not args.no_recursive, emit=print)
        return 1 if result['failed'] else 0
    except ValueError as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    raise SystemExit(main())
