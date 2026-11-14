#!/usr/bin/env python3
"""Compile the DynLex Farm catalog into its checked-in language vocabulary."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = args.compiler.resolve() if args.compiler else root / 'build/dynlex'
    if not compiler.is_file():
        parser.error('Build the DynLex compiler before generating the Farm vocabulary.')
    destination = root / 'lib/farm_challenge.dl'
    (root / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='farm-vocabulary-', dir=root / 'build') as temporary:
        program = Path(temporary) / 'language.out'
        subprocess.run([str(compiler), 'scripts/farm_vocabulary/language.dl', '-O2', '-o', str(program)],
                       cwd=root, check=True)
        text = subprocess.run([str(program)], cwd=root, check=True, capture_output=True, text=True).stdout
    changed = not destination.exists() or destination.read_text() != text
    if args.check:
        if changed:
            print(f'Out of date: {destination.relative_to(root)}')
        return int(changed)
    if changed:
        staged = destination.with_suffix('.dl.new')
        staged.write_text(text)
        staged.replace(destination)
    print('Generated the Farm language vocabulary from DynLex.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
