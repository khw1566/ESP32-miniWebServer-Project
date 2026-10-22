"""Export editable service pages to Main WebFlie. Existing user files are preserved by default."""
import argparse
import json
import re
from pathlib import Path
from generate_web_gzip import page

ROOT = Path(__file__).resolve().parents[1]
STRING = re.compile(r'"(?:\\.|[^"\\])*"')

def literal(name, filename):
    source = (ROOT / 'main' / filename).read_text(encoding='utf-8')
    match = re.search(r'\bchar\s+' + name + r'\[\]\s*=', source)
    if not match:
        raise ValueError(name)
    source = source[match.end():]
    quote = escaped = False
    for end, ch in enumerate(source):
        if escaped:
            escaped = False
        elif quote and ch == '\\':
            escaped = True
        elif ch == '"':
            quote = not quote
        elif ch == ';' and not quote:
            expression = source[:end]
            if STRING.sub('', expression).strip():
                raise ValueError(f'Unexpected C expression: {name}')
            return ''.join(json.loads(m[0]) for m in STRING.finditer(expression)).encode('utf-8')
    raise ValueError(f'Unterminated asset: {name}')

def service_pages():
    return {
        'index.html': (ROOT / 'web/index.html').read_bytes(),
        'admin.html': page('HTML_ADMIN_DASHBOARD', 'web_page_admin.c'),
        'ai_studio.html': page('HTML_AI_STUDIO', 'web_page_ai.c'),
        'help.html': literal('HTML_HELP_GUIDE', 'web_page_static.c'),
        'setup.html': literal('HTML_WIFI_SETUP', 'web_wifi_setup_html.inc'),
        'ai.html': literal('HTML_AI_LOGIN', 'web_ai_login.inc'),
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--output', type=Path, help='Explicit output folder')
    group.add_argument('--sd', type=Path, help='SD root; appends Main WebFlie')
    parser.add_argument('--replace', action='store_true', help='Replace existing pages with current source defaults')
    args = parser.parse_args()
    destination = args.output or ((args.sd / 'Main WebFlie') if args.sd else ROOT / 'sd_assets/Main WebFlie')
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in service_pages().items():
        path = destination / name
        if path.exists() and not args.replace:
            print(f'Keep: {name}')
            continue
        path.write_bytes(data)
        print(f'Write: {name} ({len(data)} bytes)')
    print(f'Folder: {destination.resolve()}')

if __name__ == '__main__':
    main()
