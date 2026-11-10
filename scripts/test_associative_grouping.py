#!/usr/bin/env python3
"""Check source-declared associativity without recognizing operator spellings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()


def check_program(root, name, program, expected_exit, expected_output="", expected_warnings=0):
    source = root / f"{name}.dl"
    source.write_text(program)
    output = root / (name + (".exe" if os.name == "nt" else ".out"))
    built = subprocess.run([str(compiler), str(source), "--no-incremental", "-o", str(output)], capture_output=True, text=True)
    assert built.returncode == 0, (name, built.stdout + built.stderr)
    assert built.stderr.count("Warning:") == expected_warnings, (name, built.stderr)
    result = subprocess.run([str(output)], capture_output=True, text=True)
    assert result.returncode == expected_exit, (name, result.returncode, result.stderr)
    assert result.stdout == expected_output, (name, result.stdout)
    print(f"{name}: passed")


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
    ("whole_group_left", "left", "(9 woven 3 woven 1)", 5),
    ("whole_group_right", "right", "(9 woven 3 woven 1)", 7),
    ("long", "left", "100" + " woven 1" * 12, 88),
    ("choice", "left", "9 woven 3 braided 1", 5),
]
with tempfile.TemporaryDirectory(prefix="dynlex-associativity-") as directory:
    root = Path(directory)
    for name, direction, expression, expected in cases:
        definition = prelude.format(direction=direction)
        if name == "choice":
            definition = definition.replace("left woven right", "left [woven|braided] right")
        check_program(root, name, definition + f'@intrinsic("return", {expression})\n', expected)

    mixed = '''boolean means: @intrinsic("type", "bool")
nothing means: @intrinsic("type", "nothing")
yes means: @intrinsic("equal", 0, 0)
no means: @intrinsic("equal", 0, 1)
function {boolean:left} and {boolean:right}:
    associativity: left
    replacement:
        @intrinsic("and", left, right)
function {nothing:left} and {nothing:right}:
    replacement:
        @intrinsic("discard", left)
        @intrinsic("discard", right)
function action:
    replacement:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%d\\n", 7))
function show value:
    replacement:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%d\\n", value))
'''
    check_program(root, "mixed_overloads", mixed + "action and show yes and no\n", 0, "7\n0\n")
    check_program(root, "mixed_explicit", mixed + "action and (show (yes and no))\n", 0, "7\n0\n")
    check_program(root, "mixed_long", mixed + "action and show " + "yes and " * 12 + "no\n", 0, "7\n0\n")

    wrapped = prelude.format(direction="left") + '''function doubled value:
    replacement:
        @intrinsic("add", value, value)
@intrinsic("return", 9 woven doubled 3 woven 1)
'''
    check_program(root, "prefix_scope_ambiguity", wrapped, 2, expected_warnings=1)

    choice_wrapper = '''integer means: @intrinsic("type", "int")
function {integer:left} woven {integer:right}:
    associativity: left
    execute:
        @intrinsic("return", @intrinsic("subtract", left, right))
function [left woven right|wrapped right]:
    replacement:
        @intrinsic("store", left, 2)
        @intrinsic("multiply", left, right)
@intrinsic("return", 9 woven wrapped 3 woven 1)
'''
    check_program(root, "choice_prefix_boundary", choice_wrapper, 2, expected_warnings=1)

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
