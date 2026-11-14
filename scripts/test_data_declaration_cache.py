#!/usr/bin/env python3
"""Named data remains visible to function bodies across cached source edits."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    compiler = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[1]
    original = (root / 'tests/required/data_declaration_function_globals/main.dl').read_text()
    with tempfile.TemporaryDirectory(prefix='dynlex-declaration-cache-') as temporary:
        folder = Path(temporary)
        source = folder / 'main.dl'
        output = folder / 'main.out'
        cache = folder / 'cache'
        changed = original.replace('42', '43')
        renamed = changed.replace('sample entry', 'another answer')
        for text in [original, changed, renamed, renamed, original]:
            source.write_text(text)
            for arguments in [['--cache-dir', str(cache)], ['--no-incremental']]:
                compiled = subprocess.run([str(compiler), str(source), *arguments, '-o', str(output)],
                                          check=True, text=True, capture_output=True, timeout=15)
                if compiled.stderr:
                    raise AssertionError(compiled.stderr)
                result = subprocess.run([str(output)], check=True, text=True, capture_output=True, timeout=5)
                if result.stdout or result.stderr:
                    raise AssertionError((result.stdout, result.stderr))
    # Runtime-backed descriptions must keep one snapshot when readers are
    # reused, other declarations move, or the initializer changes.
    fixture = root / 'tests/required/data_section_cross_function_value/main.dl'
    original = fixture.read_text()
    changed = original.replace('    1\n', '    2\n')
    reordered = changed.replace('numbers first palette:', 'numbers unused palette:\n    9\nnumbers first palette:')
    renamed = reordered.replace('first palette', 'new palette')
    with tempfile.TemporaryDirectory(prefix='dynlex-snapshot-cache-') as temporary:
        folder = Path(temporary)
        source, output, cache = folder / 'main.dl', folder / 'main.out', folder / 'cache'
        for text, expected in [(original, '1\n'), (changed, '2\n'), (reordered, '2\n'),
                               (renamed, '2\n'), (renamed, '2\n'), (original, '1\n')]:
            source.write_text(text)
            for arguments in [['--cache-dir', str(cache)], ['--no-incremental']]:
                compiled = subprocess.run([str(compiler), str(source), *arguments, '-O0', '-o', str(output)],
                                          check=True, text=True, capture_output=True, timeout=15)
                if compiled.stderr:
                    raise AssertionError(compiled.stderr)
                result = subprocess.run([str(output)], check=True, text=True, capture_output=True, timeout=5)
                if result.stdout != expected or result.stderr:
                    raise AssertionError((expected, result.stdout, result.stderr))
    print('Named data preserves static descriptions and runtime snapshots across cached edits, reordering and renames.')


if __name__ == '__main__':
    main()
