#!/usr/bin/env python3
"""Check DynLex website controllers against recording browser capabilities."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from source_package import SourcePackage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'build/dynlex')
    parser.add_argument('--case', action='append', choices=['site_controllers', 'site_language', 'site_entry', 'site_shader'])
    args = parser.parse_args()
    package = SourcePackage(ROOT / 'site/dependencies/dynlex-ui.zip')
    with tempfile.TemporaryDirectory(prefix='dynlex-site-test-') as directory:
        project = Path(directory)
        for source in (ROOT / 'site').rglob('*.dl'):
            target = project / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        package.extract(project / 'site/packages/dynlex-ui')
        fixture = project / 'tests/web/site_fixture.dl'
        fixture.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'tests/web/site_fixture.dl', fixture)
        for name in args.case or ['site_controllers', 'site_language', 'site_entry', 'site_shader']:
            source = project / f'tests/web/{name}.dl'
            source.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / f'tests/web/{name}.dl', source)
            binary = project / f'{name}.wasm'
            subprocess.run([str(args.compiler.resolve()), str(source), '--emit-wasm', '-o', str(binary)],
                           cwd=ROOT, check=True, timeout=180)
            subprocess.run(['node', str(ROOT / f'tests/web/{name}.mjs'), str(binary)],
                           cwd=ROOT, check=True, timeout=30)


if __name__ == '__main__':
    main()
