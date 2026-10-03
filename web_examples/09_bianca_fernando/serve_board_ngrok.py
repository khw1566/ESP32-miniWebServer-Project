"""Serve only board/ through the existing ngrok; no ESP32 dependencies."""
import argparse
import functools
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent / "board"
ENTRY = "bianca_fernando.html"


class BoardHandler(SimpleHTTPRequestHandler):
    def list_directory(self, path):
        self.send_error(403, "Directory listing is disabled")
        return None

    def send_head(self):
        self._remaining = None
        if urlsplit(self.path).path == "/":
            self.path = "/" + ENTRY
        target = Path(self.translate_path(self.path)).resolve()
        # Prevent symlinks/junctions from exposing files outside board/.
        try:
            target.relative_to(ROOT.resolve())
        except ValueError:
            self.send_error(403, "Outside the web folder")
            return None
        raw_range = self.headers.get("Range")
        if not raw_range or not target.is_file():
            return super().send_head()
        # Single byte ranges allow the browser to seek in the local MP3.
        match = re.fullmatch(r"bytes=(\d*)-(\d*)", raw_range.strip())
        if not match or not any(match.groups()):
            return super().send_head()
        try:
            stream = target.open("rb")
        except OSError:
            self.send_error(404, "File not found")
            return None
        size = os.fstat(stream.fileno()).st_size
        first, last = match.groups()
        if first:
            start = int(first)
            end = min(int(last), size - 1) if last else size - 1
        else:
            start, end = max(0, size - int(last)), size - 1
        if size == 0 or start > end or start >= size:
            stream.close()
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None
        self._remaining = end - start + 1
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(str(target)))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(self._remaining))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        stream.seek(start)
        return stream

    def copyfile(self, source, outputfile):
        if self._remaining is None:
            return super().copyfile(source, outputfile)
        remaining = self._remaining
        while remaining:
            block = source.read(min(65536, remaining))
            if not block:
                break
            outputfile.write(block)
            remaining -= len(block)


def find_ngrok(explicit=None):
    if explicit:
        candidates = [explicit]
    else:
        candidates = []
        if os.environ.get("NGROK_EXE"):
            candidates.append(os.environ["NGROK_EXE"])
        # The installed Python312 ngrok launcher works even when the Store
        # execution alias cannot be launched in the current environment.
        programs = Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Python"
        candidates.extend(str(p) for p in sorted(programs.glob("Python*/Scripts/ngrok.exe"), reverse=True))
        if shutil.which("ngrok"):
            candidates.append(shutil.which("ngrok"))
    for executable in dict.fromkeys(candidates):
        try:
            probe = subprocess.run([executable, "version"], capture_output=True,
                                   text=True, timeout=10)
            if probe.returncode == 0:
                return executable
        except (OSError, subprocess.TimeoutExpired):
            continue
    raise RuntimeError("No runnable ngrok found. Set NGROK_EXE or use --ngrok PATH.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", type=int, default=8080)
    parser.add_argument("--ngrok", help="Path to an existing ngrok executable")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("Port must be between 1 and 65535")
    server = None
    tunnel = None
    try:
        if not (ROOT / ENTRY).is_file():
            raise RuntimeError(f"Missing web page: {ROOT / ENTRY}")
        executable = find_ngrok(args.ngrok)
        handler = functools.partial(BoardHandler, directory=str(ROOT))
        server = ThreadingHTTPServer(("127.0.0.1", args.port), handler)
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        print(f"Web folder: {ROOT}", flush=True)
        print(f"Local URL: http://127.0.0.1:{args.port}/", flush=True)
        print(f"ngrok: {executable}", flush=True)
        print("Open the HTTPS Forwarding URL below. Press Ctrl+C to stop both.", flush=True)
        # Keep the existing ngrok configuration and authentication unchanged.
        tunnel = subprocess.Popen([executable, "http", f"http://127.0.0.1:{args.port}"])
        return tunnel.wait()
    except KeyboardInterrupt:
        return 0
    except (OSError, RuntimeError) as error:
        print(f"[ERROR] {error}", flush=True)
        return 1
    finally:
        if tunnel is not None and tunnel.poll() is None:
            tunnel.terminate()
            try:
                tunnel.wait(timeout=5)
            except subprocess.TimeoutExpired:
                tunnel.kill()
                tunnel.wait()
        if server is not None:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
