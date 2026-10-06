#!/usr/bin/env python3
"""Keep string, comment, continuation, and source-character scans consistent."""
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(sys.argv[1]).resolve()
compiler = Path(sys.argv[2]).resolve()
slash = chr(92)


def literal(value):
    return '"' + value.replace(slash, slash * 2).replace('"', slash + '"') + '"'


values = []
for count in range(9):
    values.extend(['tail' + slash * count, 'middle' + slash * count + '"#[]()end'])
values.append('reserved\x07' + slash * 2)
statements = []
for value in values:
    statements.append('''@intrinsic("discard", (
    @intrinsic("call", "libc", "puts", @intrinsic("type", "int"),
        %s # comment containing unmatched " and brackets ) ]
    )
))
''' % literal(value))

with tempfile.TemporaryDirectory(prefix="dynlex-string-boundaries-") as directory:
    temporary = Path(directory)
    source = temporary / 'strings.dl'
    source.write_text(''.join(statements))
    output = temporary / ('strings.exe' if sys.platform == 'win32' else 'strings.out')
    built = subprocess.run([str(compiler), str(source), '-o', str(output)], cwd=project,
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    assert not built.stdout + built.stderr, built.stdout + built.stderr
    result = subprocess.run([str(output)], capture_output=True, check=True)
    assert result.stdout.replace(b'\r\n', b'\n') == ''.join(value + '\n' for value in values).encode()
    print('String contents, even/odd escape runs, comments, and multiline brackets passed')

    failures = [
        ('unterminated', '@intrinsic("discard", "tail' + slash * 3 + '")\n', 'unmatched string character'),
        ('reserved_after', '@intrinsic("discard", ' + literal('tail' + slash) + ')\x07\n', 'U+0007'),
        ('reserved_comment', '@intrinsic("discard", ' + literal('tail' + slash) + ') # \x07\n', 'U+0007'),
    ]
    for name, text, diagnostic in failures:
        source = temporary / (name + '.dl')
        source.write_text(text)
        built = subprocess.run([str(compiler), str(source), '--emit-llvm', '-o', str(temporary / (name + '.ll'))],
                               cwd=project, capture_output=True, text=True)
        assert built.returncode > 0, (name, built.returncode, built.stdout, built.stderr)
        assert diagnostic in built.stdout + built.stderr, (name, built.stdout, built.stderr)
        print(name + ': passed')
