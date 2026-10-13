"""Read-only ESP-IDF metadata checks. Does not compile, configure or flash."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shlex


def read(path):
    return path.read_text(encoding='utf-8', errors='replace') if path.is_file() else ''


def version(path, prefix):
    text = read(path)
    values = [re.search(r'^#define\s+' + prefix + '_' + key + r'\s+(\d+)\b', text, re.M)
              for key in ('MAJOR', 'MINOR', 'PATCH')]
    return '.'.join(m[1] for m in values) if all(values) else None


def preflight(project, idf=None, sources=()):
    project = Path(project).resolve()
    findings = []
    build = project / 'build'
    cache = dict(re.findall(r'^([^#/:][^:=]*):[^=]*=(.*)$', read(build / 'CMakeCache.txt'), re.M))
    config = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', read(project / 'sdkconfig'), re.M))
    defaults = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', read(project / 'sdkconfig.defaults'), re.M))
    target = config.get('CONFIG_IDF_TARGET', defaults.get('CONFIG_IDF_TARGET', '')).strip('"')
    sdk = Path(idf or cache.get('IDF_PATH') or os.environ.get('IDF_PATH') or '') if (idf or cache.get('IDF_PATH') or os.environ.get('IDF_PATH')) else None
    if not (project / 'CMakeLists.txt').is_file():
        findings.append({'level': 'error', 'code': 'not_cmake_project'})
    home = cache.get('CMAKE_HOME_DIRECTORY')
    matches = Path(home).resolve() == project if home else None
    if matches is False:
        findings.append({'level': 'error', 'code': 'stale_cmake_project_directory'})
    elif matches is None:
        findings.append({'level': 'warning', 'code': 'cmake_cache_missing'})
    sdk_version = version(sdk / 'components/esp_common/include/esp_idf_version.h', 'ESP_IDF_VERSION') if sdk else None
    if not sdk_version:
        findings.append({'level': 'warning', 'code': 'idf_version_unavailable', 'hint': 'Pass --idf with the installed SDK directory.'})
    if target and cache.get('IDF_TARGET') and target != cache['IDF_TARGET']:
        findings.append({'level': 'error', 'code': 'cmake_target_mismatch'})
    db_path = build / 'compile_commands.json'
    db = []
    if db_path.is_file():
        try:
            db = json.loads(read(db_path))
            if not isinstance(db, list) or any(not isinstance(entry, dict) for entry in db):
                raise ValueError('expected array of command entries')
        except (ValueError, TypeError):
            findings.append({'level': 'error', 'code': 'invalid_compile_database'})
            db = []
    else:
        findings.append({'level': 'warning', 'code': 'compile_database_missing'})
    compiled = set()
    for entry in db:
        file = Path(entry.get('file', ''))
        if not file.is_absolute():
            file = Path(entry.get('directory', str(project))) / file
        compiled.add(file.resolve())
    cmake = re.sub(r'#[^\n]*', '', read(project / 'main/CMakeLists.txt'))
    registered = set(re.findall(r'"([^"\n]+\.(?:c|cc|cpp|cxx|S))"', cmake))
    selected = []
    for source in sources:
        file = (project / source).resolve()
        if not file.is_relative_to(project):
            findings.append({'level': 'error', 'code': 'source_outside_project'})
            continue
        relative = file.relative_to(project).as_posix()
        source_registered = file.parent == project / 'main' and file.name in registered
        selected.append({'source': relative, 'exists': file.is_file(), 'in_compile_database': file in compiled,
                         'in_main_literal_source_list': source_registered})
        if not file.is_file():
            findings.append({'level': 'error', 'code': 'source_missing', 'source': relative})
        elif db and file not in compiled:
            findings.append({'level': 'warning', 'code': 'source_missing_from_existing_database', 'source': relative})
        if file.parent == project / 'main' and not source_registered:
            findings.append({'level': 'warning', 'code': 'check_component_source_registration', 'source': relative})
    compiler = cache.get('CMAKE_C_COMPILER')
    if not compiler and db:
        args = db[0].get('arguments')
        try:
            compiler = args[0] if args else shlex.split(db[0].get('command', ''), posix=False)[0].strip('"')
        except (IndexError, ValueError):
            findings.append({'level': 'warning', 'code': 'compile_command_not_parseable'})
    if compiler and not Path(compiler).is_file():
        findings.append({'level': 'warning', 'code': 'recorded_compiler_not_found'})
    return {'project': str(project), 'target': target or None, 'idf_version': sdk_version,
            'lvgl_version': version(project / 'managed_components/lvgl__lvgl/lv_version.h', 'LVGL_VERSION'),
            'cmake_project_matches': matches, 'compile_database_entries': len(db),
            'compile_database_modified_utc': datetime.datetime.fromtimestamp(db_path.stat().st_mtime, datetime.timezone.utc).isoformat() if db_path.is_file() else None,
            'selected_sources': selected, 'findings': findings, 'compiler_executed': False,
            'evidence': 'metadata_only_not_compilation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', required=True)
    parser.add_argument('--idf')
    parser.add_argument('--source', action='append', default=[])
    args = parser.parse_args()
    report = preflight(args.project, args.idf, args.source)
    print(json.dumps(report, indent=2, ensure_ascii=True))
    return 1 if any(f['level'] == 'error' for f in report['findings']) else 0


if __name__ == '__main__':
    raise SystemExit(main())
