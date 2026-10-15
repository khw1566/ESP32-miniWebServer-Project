"""Prepare bounded M3 gallery assets with an existing FFmpeg installation.

Keeps the source, refuses to replace outputs, never touches a board or builds C.
"""
from pathlib import Path
import argparse
import shutil
import subprocess

VIDEO = {'.mp4', '.mov', '.webm', '.mkv', '.avi', '.gif', '.m4v', '.mjpg', '.mjpeg'}
IMAGE = {'.jpg', '.jpeg', '.png', '.bmp', '.webp', '.heic', '.heif', '.tif', '.tiff', '.avif'}


def command(source, destination, ffmpeg='ffmpeg', native_mp4=False):
    source, destination = Path(source), Path(destination)
    video = source.suffix.lower() in VIDEO
    if source.suffix.lower() not in VIDEO | IMAGE:
        raise ValueError('Unsupported input extension')
    if source.resolve() == destination.resolve():
        raise ValueError('Source and destination must differ')
    if native_mp4:
        if not video or destination.suffix.lower() != '.mp4':
            raise ValueError('Native playback needs a video input and a new .mp4 output')
        return [ffmpeg, '-hide_banner', '-loglevel', 'error', '-n', '-i', str(source),
                '-map', '0:v:0', '-map', '0:a:0?', '-sn', '-dn', '-t', '1800',
                '-vf', 'fps=15,scale=480:272:force_original_aspect_ratio=decrease:force_divisible_by=16',
                '-c:v', 'libx264', '-profile:v', 'baseline', '-level:v', '3.0', '-pix_fmt', 'yuv420p',
                '-bf', '0', '-refs', '1', '-g', '30', '-b:v', '700k', '-maxrate', '900k', '-bufsize', '1800k',
                '-x264-params', 'cabac=0:bframes=0:ref=1', '-c:a', 'aac', '-profile:a', 'aac_low',
                '-ar', '44100', '-ac', '2', '-b:a', '96k', '-movflags', '+faststart',
                '-use_editlist', '0', str(destination)]
    if destination.suffix.lower() != ('.mjpg' if video else '.png'):
        raise ValueError('Use .mjpg for video/animated GIF, .png for images')
    args = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-n', '-i', str(source), '-map', '0:v:0', '-an']
    scale = 'scale=432:480:force_original_aspect_ratio=decrease:force_divisible_by=2'
    if video:
        args += ['-vf', 'fps=10,' + scale, '-c:v', 'mjpeg', '-q:v', '7', '-f', 'mjpeg']
    else:
        args += ['-vf', scale, '-frames:v', '1']
    return args + [str(destination)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--native-mp4', action='store_true', help='Prepare H.264 Baseline + AAC-LC MP4 with sound')
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error('Source file is missing')
    if args.destination.exists():
        parser.error('Destination already exists; choose a new filename')
    if not args.destination.parent.is_dir():
        parser.error('Destination directory is missing')
    executable = shutil.which(args.ffmpeg)
    if not executable:
        parser.error('FFmpeg is not installed or not on PATH; supply --ffmpeg with its path')
    try:
        subprocess.run(command(args.source, args.destination, executable, args.native_mp4), check=True)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    print(f'Created {args.destination}. Source preserved.')


if __name__ == '__main__':
    main()
