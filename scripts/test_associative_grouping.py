#!/usr/bin/env python3
"""Check source-declared associativity without recognizing operator spellings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()
prelude = '''function left woven right:
    associativity: {direction}
    execute:
        @intrinsic("return", @intrinsic("subtract", left, right))

'''
cases = [
    ("left", "left", "9 woven 3 woven 1", 5),
    ("right", "right", "9 woven 3 woven 1", 7),
    ("explicit_right", "left", "9 woven (3 woven 1)", 7),
    ("explicit_left", "right", "(9 woven 3) woven 1", 5),
    ("long", "left", "100" + " woven 1" * 12, 88),
    ("choice", "left", "9 woven 3 braided 1", 5),
]
with tempfile.TemporaryDirectory(prefix="dynlex-associativity-") as directory:
    root = Path(directory)
    for name, direction, expression, expected in cases:
        source = root / f"{name}.dl"
        definition = prelude.format(direction=direction)
        if name == "choice":
            definition = definition.replace("left woven right", "left [woven|braided] right")
        source.write_text(definition + f'@intrinsic("return", {expression})\n')
        output = root / (name + (".exe" if os.name == "nt" else ".out"))
        built = subprocess.run([str(compiler), str(source), "-o", str(output)], capture_output=True, text=True)
        assert built.returncode == 0, (name, built.stdout + built.stderr)
        assert "Warning:" not in built.stderr, (name, built.stderr)
        result = subprocess.run([str(output)], capture_output=True, text=True)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        print(f"{name}: passed")

    invalid = {
        "direction": prelude.replace("{direction}", "up"),
        "duplicate": prelude.replace("{direction}", "left, right"),
        "empty": prelude.replace("{direction}", ""),
        "prefix": prelude.replace("{direction}", "left").replace("left woven right", "woven right"),
        "conflict": '''integer means: @intrinsic("type", "int")
floating means: @intrinsic("type", "float")
function {integer:left} woven {integer:right}:
    associativity: left
    execute:
        @intrinsic("return", @intrinsic("subtract", left, right))
function {floating:left} woven {floating:right}:
    associativity: right
    execute:
        @intrinsic("return", @intrinsic("subtract", left, right))
''',
    }
    for name, program in invalid.items():
        source = root / f"invalid_{name}.dl"
        source.write_text(program)
        result = subprocess.run([str(compiler), str(source), "--emit-llvm", "-o", str(root / "invalid.ll")], capture_output=True, text=True)
        assert result.returncode == 1, (name, result.returncode, result.stdout + result.stderr)
        assert "associativ" in result.stderr.lower(), (name, result.stderr)
        print(f"invalid {name}: passed")

    localized = root / "localized"
    localized.mkdir()
    (localized / "config.dl").write_text('''dynlex options:
    child sections:
        associativity: "grouping"
    associativity:
        left: "forward"
        right: "backward"
''')
    source = localized / "main.dl"
    source.write_text(prelude.format(direction="forward").replace("associativity:", "grouping:")
                      + '@intrinsic("return", 9 woven 3 woven 1)\n')
    result = subprocess.run([str(compiler), str(source), "--emit-llvm", "-o", str(localized / "main.ll")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert "Warning:" not in result.stderr, result.stderr
    print("configured spelling: passed")

    for name, expression in {
        "standard_or": "true or false or true",
        "standard_and": "true and true and false",
        "standard_sum": "1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10",
    }.items():
        source = root / f"{name}.dl"
        source.write_text(f'import lib/std.dl\n@intrinsic("return", ({expression}) as an integer)\n')
        result = subprocess.run([str(compiler), str(source), "--emit-llvm", "-o", str(root / f"{name}.ll")], capture_output=True, text=True)
        assert result.returncode == 0, (name, result.stderr)
        assert "Warning:" not in result.stderr, (name, result.stderr)
        print(f"{name}: passed")
