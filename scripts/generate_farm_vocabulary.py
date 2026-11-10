#!/usr/bin/env python3
"""Compile the DynLex Farm catalog into its checked-in language and browser data."""
import argparse
import json
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
    outputs = {}
    (root / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='farm-vocabulary-', dir=root / 'build') as temporary:
        for entry, destination in [
            ('scripts/farm_vocabulary/language.dl', 'lib/farm_challenge.dl'),
            ('site/games/farm/export_vocabulary.dl', 'web/farm-vocabulary.json'),
        ]:
            program = Path(temporary) / (Path(entry).stem + '.out')
            subprocess.run([str(compiler), entry, '-O2', '-o', str(program)], cwd=root, check=True)
            text = subprocess.run([str(program)], cwd=root, check=True, capture_output=True, text=True).stdout
            if destination.endswith('.json'):
                json.loads(text)
            outputs[root / destination] = text
    mismatches = [path for path, text in outputs.items() if not path.exists() or path.read_text() != text]
    if args.check:
        for path in mismatches:
            print(f'Out of date: {path.relative_to(root)}')
        return int(bool(mismatches))
    for path in mismatches:
        staged = path.with_suffix(path.suffix + '.new')
        staged.write_text(outputs[path])
        staged.replace(path)
    print('Generated the Farm language and browser catalog from DynLex.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
