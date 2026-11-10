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
    cases = ['site_controllers', 'site_language', 'site_entry', 'site_shader', 'site_program_events', 'site_river_controller', 'site_river_audio', 'site_progress_controller', 'site_account_profiles', 'site_account_controller', 'site_account_recovery', 'site_reply_ownership']
    parser.add_argument('--case', action='append', choices=cases)
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
        shutil.copyfile(ROOT / 'tests/web/site_account_support.dl', fixture.with_name('site_account_support.dl'))
        for name in args.case or cases:
            source = project / f'tests/web/{name}.dl'
            source.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / f'tests/web/{name}.dl', source)
        for name in args.case or cases:
            source = project / f'tests/web/{name}.dl'
            binary = project / f'{name}.wasm'
            subprocess.run([str(args.compiler.resolve()), str(source), '--emit-wasm', '-o', str(binary)],
                           cwd=ROOT, check=True, timeout=180)
            subprocess.run(['node', str(ROOT / f'tests/web/{name}.mjs'), str(binary)],
                           cwd=ROOT, check=True, timeout=30)


if __name__ == '__main__':
    main()
