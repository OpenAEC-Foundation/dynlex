#!/usr/bin/env python3
"""Check nested ambiguity only after the complete candidate type-checks."""
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(sys.argv[1]).resolve()
compiler = Path(sys.argv[2]).resolve()
prelude = '''function left - right:
    execute:
        @intrinsic("return", @intrinsic("subtract", left, right))

function [take] value [done]:
    execute:
        @intrinsic("return", value)

function [pair] first [with] second [done]:
    execute:
        @intrinsic("return", @intrinsic("add", @intrinsic("multiply", first, 10), second))

function [show] value:
    execute:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%d\\n", value))

'''
cases = [
    ("delimited", "show take 3 - 0 - 2 done\n", "1\n", 1),
    ("parenthesized", "show take (3 - 0 - 2) done\n", "1\n", 1),
    ("independent", "show pair (3 - 0 - 2) with (3 - 0 - 2) done\n", "11\n", 1),
    ("explicit", "show take ((3 - 0) - 2) done\nshow take (3 - (0 - 2)) done\n", "1\n5\n", 0),
    ("type_filtered", '''class:
    patterns: box
    members: item

function [boxed] value:
    execute:
        @intrinsic("return", @intrinsic("construct", box, value))

function value [negated]:
    execute:
        @intrinsic("return", @intrinsic("negate", value))

function [the content of] value [box]:
    execute:
        @intrinsic("return", value's item)

show the content of (boxed 3 negated) box
''', "-3\n", 0),
]

with tempfile.TemporaryDirectory(prefix="dynlex-nested-grouping-") as directory:
    temporary = Path(directory)
    for name, body, expected, warning_count in cases:
        source = temporary / f"{name}.dl"
        source.write_text(prelude + body)
        output = temporary / (name + (".exe" if sys.platform == "win32" else ".out"))
        built = subprocess.run(
            [str(compiler), str(source), "-o", str(output)],
            cwd=project, capture_output=True, text=True, check=True,
        )
        diagnostics = built.stdout + built.stderr
        actual_count = diagnostics.count("has multiple valid operand groupings")
        assert actual_count == warning_count, (name, warning_count, diagnostics)
        result = subprocess.run([str(output)], capture_output=True, text=True, check=True)
        assert result.stdout == expected, (name, expected, result.stdout)
        print(f"{name}: passed")
