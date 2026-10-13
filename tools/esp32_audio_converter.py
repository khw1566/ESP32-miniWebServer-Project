"""Folder audio converter for ESP32; run without arguments for the Korean GUI."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading


EXTENSIONS = {'.mp3', '.wav', '.flac', '.m4a', '.aac', '.ogg', '.opus', '.wma', '.aif', '.aiff', '.mp4', '.webm'}
PROFILES = {
    'mp3-small': ('MP3 최소 용량: 32kbps / 16kHz / 모노', '.mp3', '16000', ['-c:a', 'libmp3lame', '-b:a', '32k']),
    'mp3-balanced': ('MP3 음악용: 64kbps / 24kHz / 모노', '.mp3', '24000', ['-c:a', 'libmp3lame', '-b:a', '64k']),
    'wav-easy': ('WAV 간단 재생: PCM16 / 16kHz / 모노', '.wav', '16000', ['-c:a', 'pcm_s16le']),
}


def find_ffmpeg(explicit: str | None = None) -> str:
    candidates = [explicit] if explicit else [
        shutil.which('ffmpeg'),
        str(Path(__file__).parent / 'ffmpeg.exe'),
        r'C:\KHW\ffmpeg\bin\ffmpeg.exe',
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise ValueError('FFmpeg를 찾을 수 없습니다. FFmpeg를 PATH에 등록하거나 tools 폴더에 ffmpeg.exe를 넣어 주세요.')


@dataclass
class Summary:
    converted: int = 0
    skipped: int = 0
    failed: int = 0
    before: int = 0
    after: int = 0
    cancelled: bool = False

    def message(self) -> str:
        state = '중단' if self.cancelled else '완료'
        sizes = f' / 변환 파일 용량 {self.before / 1048576:.2f} -> {self.after / 1048576:.2f} MB'
        return f'{state}: 성공 {self.converted}, 건너뜀 {self.skipped}, 실패 {self.failed}' + sizes


def convert_folder(source: Path, output: Path, profile: str, ffmpeg: str,
                   recursive: bool = True, cancel: threading.Event | None = None,
                   emit=lambda message: None) -> Summary:
    source, output = source.resolve(), output.resolve()
    if not source.is_dir():
        raise ValueError('입력 폴더가 존재하지 않습니다.')
    if output == source or source.is_relative_to(output):
        raise ValueError('출력은 입력과 다른 폴더여야 하며, 입력의 상위 폴더도 사용할 수 없습니다.')
    _, extension, rate, codec = PROFILES[profile]
    cancel = cancel or threading.Event()
    files = []
    for directory, dirs, names in os.walk(source, followlinks=False):
        dirs[:] = sorted(d for d in dirs if not (Path(directory) / d).is_symlink()
                         and not (Path(directory) / d).resolve().is_relative_to(output)) if recursive else []
        for name in sorted(names):
            path = Path(directory) / name
            if path.suffix.lower() in EXTENSIONS and not path.is_symlink():
                files.append(path)
    output.mkdir(parents=True, exist_ok=True)
    summary = Summary()
    emit(f'음악 파일 {len(files)}개 / {PROFILES[profile][0]}')
    # Reserve destinations before starting; same-stem files receive a suffix.
    reserved = set()
    for index, path in enumerate(files, 1):
        if cancel.is_set():
            summary.cancelled = True
            break
        relative = path.relative_to(source)
        target = output / relative.with_suffix(extension)
        number = 1
        while str(target).casefold() in reserved:
            number += 1
            target = output / relative.parent / f'{relative.stem}_{number}{extension}'
        reserved.add(str(target).casefold())
        if target.exists():
            summary.skipped += 1
            emit(f'[{index}/{len(files)}] 기존 파일 보존: {target.relative_to(output)}')
            continue
        temporary = None
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.converting-', suffix=extension, dir=target.parent)
            os.close(fd)
            command = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
                       '-i', str(path), '-map', '0:a:0', '-vn', '-sn', '-dn',
                       '-map_metadata', '-1', '-map_chapters', '-1', '-ac', '1', '-ar', rate,
                       *codec]
            if extension == '.mp3':
                command += ['-id3v2_version', '0', '-write_id3v1', '0', '-write_xing', '0']
            command += [temporary]
            emit(f'[{index}/{len(files)}] 변환 중: {relative}')
            # Use a file for stderr so long decoder errors cannot block the pipe.
            with tempfile.TemporaryFile() as errors:
                process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=errors,
                                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                while process.poll() is None:
                    if cancel.wait(0.1):
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                        summary.cancelled = True
                        break
                if summary.cancelled:
                    break
                errors.seek(0)
                error = errors.read(8192).decode('utf-8', errors='replace').strip()
                if process.returncode or not Path(temporary).stat().st_size:
                    raise RuntimeError(error or 'FFmpeg가 오디오를 생성하지 못했습니다.')
            # Exclusive creation prevents overwriting a file created during conversion.
            try:
                with target.open('xb') as dest:
                    try:
                        with open(temporary, 'rb') as converted:
                            shutil.copyfileobj(converted, dest)
                    except BaseException:
                        dest.close()
                        target.unlink(missing_ok=True)
                        raise
            except FileExistsError:
                summary.skipped += 1
                emit(f'기존 파일 보존: {target.relative_to(output)}')
                continue
            summary.converted += 1
            summary.before += path.stat().st_size
            summary.after += target.stat().st_size
            emit(f'저장: {target.relative_to(output)} ({target.stat().st_size / 1024:.1f} KB)')
        except Exception as exc:
            summary.failed += 1
            emit(f'실패: {relative}: {exc}')
        finally:
            if temporary:
                Path(temporary).unlink(missing_ok=True)
    emit(summary.message())
    return summary


def launch_gui() -> None:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText

    root = tk.Tk()
    root.title('ESP32 음악 파일 변환기')
    root.geometry('820x540')
    root.minsize(640, 440)
    root.columnconfigure(1, weight=1)
    root.rowconfigure(6, weight=1)
    source, output = tk.StringVar(), tk.StringVar()
    profile = tk.StringVar(value=PROFILES['mp3-small'][0])
    recursive = tk.BooleanVar(value=True)
    events = queue.Queue()
    cancel = threading.Event()
    busy = False

    def select_source():
        selected = filedialog.askdirectory(title='음악 파일이 들어 있는 폴더')
        if selected:
            source.set(selected)
            output.set(str(Path(selected) / 'esp32_audio'))

    def select_output():
        selected = filedialog.askdirectory(title='변환 결과 저장 폴더')
        if selected:
            output.set(selected)

    for row, (label, variable, callback) in enumerate([
        ('음악 폴더', source, select_source), ('저장 폴더', output, select_output)
    ]):
        ttk.Label(root, text=label).grid(row=row, column=0, padx=10, pady=10)
        ttk.Entry(root, textvariable=variable).grid(row=row, column=1, sticky='ew')
        ttk.Button(root, text='폴더 선택', command=callback).grid(row=row, column=2, padx=10)
    ttk.Label(root, text='변환 형식').grid(row=2, column=0, padx=10)
    ttk.Combobox(root, textvariable=profile, values=[p[0] for p in PROFILES.values()],
                 state='readonly').grid(row=2, column=1, columnspan=2, sticky='ew', padx=(0, 10))
    ttk.Checkbutton(root, text='하위 폴더도 변환', variable=recursive).grid(row=3, column=1, sticky='w', pady=8)
    ttk.Label(root, text='원본과 기존 결과는 보존합니다. MP3 최소 용량: 약 240 KB/분, WAV: 약 1.92 MB/분.',
              wraplength=600).grid(row=4, column=0, columnspan=3, sticky='w', padx=10)
    controls = ttk.Frame(root)
    controls.grid(row=5, column=0, columnspan=3, sticky='ew', padx=10, pady=10)
    log = ScrolledText(root, height=12, state='disabled', wrap='word')
    log.grid(row=6, column=0, columnspan=3, sticky='nsew', padx=10, pady=(0, 10))
    status = tk.StringVar(value='폴더를 선택한 뒤 변환 시작을 누르세요.')
    ttk.Label(root, textvariable=status, wraplength=700).grid(row=7, column=0, columnspan=3, sticky='w', padx=10, pady=(0, 10))

    def start():
        nonlocal busy
        if busy:
            return
        try:
            if not source.get().strip() or not output.get().strip():
                raise ValueError('음악 폴더와 저장 폴더를 선택해 주세요.')
            ffmpeg = find_ffmpeg()
            src, dst = Path(source.get()), Path(output.get())
            key = next(k for k, value in PROFILES.items() if value[0] == profile.get())
            recurse = recursive.get()
        except Exception as exc:
            messagebox.showerror('변환 설정 확인', str(exc))
            return
        busy = True
        cancel.clear()
        start_button.configure(state='disabled')
        stop_button.configure(state='normal')
        status.set('변환 중...')

        def work():
            try:
                result = convert_folder(src, dst, key, ffmpeg, recurse, cancel,
                                        lambda text: events.put(('log', text)))
                events.put(('done', result.message()))
            except Exception as exc:
                events.put(('done', f'오류: {exc}'))
        threading.Thread(target=work, daemon=True).start()

    def stop():
        cancel.set()
        status.set('현재 변환을 중단하는 중...')

    start_button = ttk.Button(controls, text='변환 시작', command=start)
    start_button.pack(side='left')
    stop_button = ttk.Button(controls, text='중단', command=stop, state='disabled')
    stop_button.pack(side='left', padx=10)

    def poll():
        nonlocal busy
        for _ in range(100):
            try:
                kind, text = events.get_nowait()
            except queue.Empty:
                break
            if kind == 'done':
                busy = False
                start_button.configure(state='normal')
                stop_button.configure(state='disabled')
                status.set(text)
            else:
                log.configure(state='normal')
                log.insert('end', text + '\n')
                log.see('end')
                log.configure(state='disabled')
        root.after(100, poll)

    def close():
        if busy:
            stop()
            root.after(100, close)
        else:
            root.destroy()
    root.protocol('WM_DELETE_WINDOW', close)
    poll()
    root.mainloop()


def main() -> int:
    parser = argparse.ArgumentParser(description='ESP32용 음악 폴더 일괄 변환. 인자 없이 실행하면 GUI가 열립니다.')
    parser.add_argument('source', nargs='?', type=Path)
    parser.add_argument('-o', '--output', type=Path)
    parser.add_argument('--profile', choices=PROFILES, default='mp3-small')
    parser.add_argument('--ffmpeg', help='ffmpeg 실행 파일 경로')
    parser.add_argument('--no-recursive', action='store_true')
    args = parser.parse_args()
    if args.source is None:
        launch_gui()
        return 0
    try:
        result = convert_folder(args.source, args.output or args.source / 'esp32_audio',
                                args.profile, find_ffmpeg(args.ffmpeg), not args.no_recursive,
                                emit=print)
        return 1 if result.failed else 0
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
