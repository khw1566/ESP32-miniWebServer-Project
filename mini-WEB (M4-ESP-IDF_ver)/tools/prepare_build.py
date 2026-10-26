"""Stage public M4 build inputs; never activates IDF or invokes a compiler."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def prepare(destination: Path):
    destination = destination.resolve()
    if destination == ROOT or destination in ROOT.parents or ROOT in destination.parents:
        raise ValueError('Stage must be outside the source project')
    destination.mkdir(parents=True, exist_ok=True)
    # Fresh stage per build: no stale headers, dependencies or firmware outputs.
    if any(destination.iterdir()):
        raise ValueError('Stage must be empty')
    for directory in ('main', 'cmake', 'components', 'third_party'):
        for source in (ROOT / directory).rglob('*'):
            if not source.is_file():
                continue
            relative = source.relative_to(ROOT)
            if source.is_symlink() or '.private' in relative.parts or source.name.endswith('_private.h'):
                raise ValueError('Private/symlink input is not a public build source')
            if '__pycache__' in relative.parts:
                continue
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    for name in ('CMakeLists.txt', 'sdkconfig.defaults', 'sdkconfig.panel-v2.defaults',
                 'sdkconfig.panel-v1.defaults', 'sdkconfig.rev3.defaults', 'partitions_16mb.csv'):
        shutil.copy2(ROOT / name, destination / name)
    (destination / 'tools').mkdir()
    shutil.copy2(ROOT / 'tools/generate_web_gzip.py', destination / 'tools/generate_web_gzip.py')
    if (ROOT / 'sdkconfig').is_file():
        shutil.copy2(ROOT / 'sdkconfig', destination / 'sdkconfig')
    print(f'M4 public sources staged: {destination}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    prepare(parser.parse_args().destination)
