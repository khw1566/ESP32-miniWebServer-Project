"""Source-page export and preservation checks; no compiler or physical SD access."""
from pathlib import Path
import subprocess
import sys
import tempfile
from export_main_web import ROOT, service_pages

pages = service_pages()
assert set(pages) == {'index.html', 'admin.html', 'help.html', 'setup.html', 'ai.html', 'ai_studio.html'}
for name, data in pages.items():
    assert data and b'html' in data.lower(), name
    assert (ROOT / 'sd_assets/Main WebFlie' / name).is_file(), name
results = ROOT / 'results'
results.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='main-web-export-', dir=results) as temporary:
    destination = Path(temporary).resolve()
    assert destination.is_relative_to(results.resolve())
    command = [sys.executable, '-X', 'utf8', str(ROOT / 'tools/export_main_web.py'), '--sd', str(destination)]
    subprocess.run(command, check=True, capture_output=True)
    folder = destination / 'Main WebFlie'
    for name, data in pages.items():
        assert (folder / name).read_bytes() == data
    custom = b'<html>custom user page</html>'
    (folder / 'index.html').write_bytes(custom)
    subprocess.run(command, check=True, capture_output=True)
    assert (folder / 'index.html').read_bytes() == custom
    subprocess.run(command + ['--replace'], check=True, capture_output=True)
    assert (folder / 'index.html').read_bytes() == pages['index.html']
print(f'PASS: {len(pages)} exact source pages ({sum(map(len, pages.values()))} bytes), Main WebFlie spelling, preserve existing edits, explicit replace.')
