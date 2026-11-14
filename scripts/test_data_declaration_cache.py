#!/usr/bin/env python3
"""Named data remains visible to function bodies across cached source edits."""
from pathlib import Path
import subprocess
import sys
import tempfile


def verify_edits(compiler, prefix, edits, options=()):
    with tempfile.TemporaryDirectory(prefix=prefix) as temporary:
        folder = Path(temporary)
        source = folder / 'main.dl'
        output = folder / 'main.out'
        cache = folder / 'cache'
        for text, expected in edits:
            source.write_text(text)
            for arguments in [['--cache-dir', str(cache)], ['--no-incremental']]:
                compiled = subprocess.run([str(compiler), str(source), *arguments, *options, '-o', str(output)],
                                          check=True, text=True, capture_output=True, timeout=15)
                if compiled.stderr:
                    raise AssertionError(compiled.stderr)
                result = subprocess.run([str(output)], check=True, text=True, capture_output=True, timeout=5)
                if result.stdout != expected or result.stderr:
                    raise AssertionError((expected, result.stdout, result.stderr))


def main():
    compiler = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[1]
    original = (root / 'tests/required/data_declaration_function_globals/main.dl').read_text()
    changed = original.replace('42', '43')
    renamed = changed.replace('sample entry', 'another answer')
    verify_edits(compiler, 'dynlex-declaration-cache-',
                 [(text, '') for text in [original, changed, renamed, renamed, original]])
    # Runtime-backed descriptions must keep one snapshot when readers are
    # reused, other declarations move, or the initializer changes.
    fixture = root / 'tests/required/data_section_cross_function_value/main.dl'
    original = fixture.read_text()
    changed = original.replace('    1\n', '    2\n')
    reordered = changed.replace('numbers first palette:', 'numbers unused palette:\n    9\nnumbers first palette:')
    renamed = reordered.replace('first palette', 'new palette')
    verify_edits(compiler, 'dynlex-snapshot-cache-',
                 [(original, '1\n'), (changed, '2\n'), (reordered, '2\n'),
                  (renamed, '2\n'), (renamed, '2\n'), (original, '1\n')], ['-O0'])
    original = (root / 'tests/required/data_section_main_flex_capture/main.dl').read_text()
    changed = original.replace('value: 42', 'value: 43')
    renamed = changed.replace('answer', 'named number')
    forward = (root / 'tests/required/data_section_main_flex_forward/main.dl').read_text()
    verify_edits(compiler, 'dynlex-inline-data-cache-',
                 [(original, '43\n'), (changed, '44\n'), (renamed, '44\n'),
                  (forward, '43\n'), (original, '43\n')], ['-O0'])
    original = (root / 'tests/required/data_section_callback_snapshot/main.dl').read_text()
    renamed = original.replace('callback answer', 'another callback')
    verify_edits(compiler, 'dynlex-callback-data-cache-',
                 [(text, '1 1\n') for text in [original, renamed, renamed, original]], ['-O0'])
    print('Named data preserves static descriptions, snapshots and inline captures across cached edits, reordering and renames.')


if __name__ == '__main__':
    main()
