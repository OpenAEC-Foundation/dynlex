#!/usr/bin/env python3
"""Type-check every installer smoke program before building release packages."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    project = Path(__file__).resolve().parents[1]
    compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else \
        project / 'build' / ('dynlex.exe' if os.name == 'nt' else 'dynlex')
    fixtures = sorted((project / 'tests/release').glob('*_smoke.dl'))
    assert fixtures, 'The installer smoke programs are missing'
    with tempfile.TemporaryDirectory(prefix='dynlex-release-fixtures-') as directory:
        for source in fixtures:
            subprocess.run([str(compiler), str(source), '--no-incremental', '--emit-llvm',
                            '-o', str(Path(directory) / (source.stem + '.ll'))],
                           cwd=project, check=True, timeout=30)
            print(f'PASS release fixture {source.name}', flush=True)


if __name__ == '__main__':
    main()
