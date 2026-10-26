import hashlib
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from benchmark_external_download import download, make_fixture


DATA = bytes(range(256)) * 512


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        if self.path == "/compressed":
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(DATA)))
        self.send_header("X-Relay-Transport", "stream-v2")
        self.end_headers()
        try:
            self.wfile.write(DATA)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *args):
        pass


class BenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def test_actual_http_bytes_hash_and_timing(self):
        result = download(self.base + "/file.txt", expected_sha256=hashlib.sha256(DATA).hexdigest())
        self.assertEqual(result["bytes"], len(DATA))
        self.assertEqual(result["transport"], "stream-v2")
        self.assertGreater(result["seconds"], 0)
        self.assertAlmostEqual(result["MBps"], len(DATA) / result["seconds"] / 1_000_000)
        self.assertGreaterEqual(result["seconds"], result["ttfb_seconds"])

    def test_hash_compression_and_byte_limit_rejected(self):
        with self.assertRaises(ValueError):
            download(self.base + "/file.txt", expected_sha256="0" * 64)
        with self.assertRaises(ValueError):
            download(self.base + "/compressed")
        with self.assertRaises(ValueError):
            download(self.base + "/file.txt", max_bytes=1024)

    def test_fixture_preserves_existing_file_and_reports_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.txt"
            info = make_fixture(path, 1)
            self.assertEqual(info["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(path.stat().st_size, 1024 * 1024)
            with self.assertRaises(FileExistsError):
                make_fixture(path, 1)


if __name__ == "__main__":
    unittest.main()
