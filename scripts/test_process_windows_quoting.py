#!/usr/bin/env python3
"""Exercise DynLex Windows quoting and its native callback ABI on every host."""
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(__file__).resolve().parent.parent
compiler = project / "build" / ("dynlex.exe" if sys.platform == "win32" else "dynlex")
slash = chr(92)
cases = [
    ("plain", "plain"),
    ("", '""'),
    ("two words", '"two words"'),
    ("a\tb", '"a\tb"'),
    ('a"b', '"a' + slash + '"b"'),
    ("a" + slash, "a" + slash),
    ("a " + slash, '"a ' + slash * 2 + '"'),
    ('a ' + slash * 2 + '"b', '"a ' + slash * 5 + '"b"'),
    ("日本 🌍", '"日本 🌍"'),
]


def literal(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t') + '"'


def units(text):
    data = text.encode("utf-16-le")
    return [int.from_bytes(data[index:index + 2], "little") for index in range(0, len(data), 2)]


def verify(pointer, text, prefix):
    expected = units(text)
    return f'''set {prefix}Expected to [{", ".join(map(str, expected + [0]))}]
set {prefix}Index to 0
loop while {prefix}Index < {len(expected) + 1}:
    require condition ((the item at {prefix}Index in {pointer}) = (the item at {prefix}Index in {prefix}Expected)) reporting "Unexpected UTF-16 command line"
    increment {prefix}Index
'''


source = (project / "tests/runtime/windows_command_line_limits.dl").read_text() + "\n"
for index, (original, expected) in enumerate(cases):
    prefix = f"case{index}"
    source += f'''set {prefix}Text to the string form of {literal(original)}
set {prefix}Encoded to the UTF-16 encoding of {prefix}Text
require condition ({prefix}Encoded's success) reporting "Encoding failed"
set {prefix}Input to (the data of {prefix}Encoded's storage) as a pointer to a 16 bit unsigned integer
set {prefix}Length to the Windows argument end for {prefix}Input of length {prefix}Encoded's length written to (0 as a pointer to a 16 bit unsigned integer) from 0
require condition ({prefix}Length = {len(units(expected))}) reporting "Incorrect quoted length"
set {prefix}Output to @intrinsic("call", "libc", "calloc", a pointer to a 16 bit unsigned integer, ({prefix}Length + 1) as a word sized integer, 2 as a word sized integer)
set {prefix}End to the Windows argument end for {prefix}Input of length {prefix}Encoded's length written to {prefix}Output from 0
require condition ({prefix}End = {prefix}Length) reporting "Quoting length differs between passes"
'''
    source += verify(f"{prefix}Output", expected, prefix)
    source += f'@intrinsic("call", "libc", "free", nothing, {prefix}Output)\n'

source += r'''set executable to the UTF-16 encoding of (the string form of "C:\\Program Files\\risk.exe")
set first to the string form of "日本 🌍"
set second to the string form of ""
set arguments to [
    @intrinsic("construct", a native process string, the data of first, first's length as a word sized integer),
    @intrinsic("construct", a native process string, the data of second, second's length as a word sized integer)
]
set output to @intrinsic("call pointer", (the function named "build the Windows command line for $ with $ counting $"), a pointer to a 16 bit unsigned integer, (the data of executable's storage) as a pointer to a 16 bit unsigned integer, (the address of arguments) as a pointer to a native process string, 2 as a word sized integer)
require condition (output != (0 as a pointer to a 16 bit unsigned integer)) reporting "Command-line builder failed"
'''
source += verify("output", '"C:\\Program Files\\risk.exe" "日本 🌍" ""', "command")
source += '@intrinsic("call", "libc", "free", nothing, output)\n'
with tempfile.TemporaryDirectory(prefix="dynlex-windows-quoting-") as directory:
    temporary = Path(directory)
    path = temporary / "quoting.dl"
    path.write_text(source)
    executable = temporary / ("quoting.exe" if sys.platform == "win32" else "quoting.out")
    built = subprocess.run([str(compiler), str(path), "-o", str(executable)], cwd=project, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    assert not built.stdout + built.stderr, built.stdout + built.stderr
    subprocess.run([str(executable)], check=True)
print("Windows quoting, Unicode conversion, and command-line callback passed")
