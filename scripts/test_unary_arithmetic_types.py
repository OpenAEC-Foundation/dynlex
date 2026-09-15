#!/usr/bin/env python3
"""Unary arithmetic accepts numeric scalar/vector values, never object storage."""
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = Path(sys.argv[1]).resolve()
operations = ("negate", "sin", "cos", "sqrt", "abs", "floor", "ceil", "round", "exp", "log")
class_prefix = "class:\n    patterns: box\n    members: item\n\n"
integer = '@intrinsic("type", "int", 32)'
floating = '@intrinsic("type", "float", 32)'
vector = f'@intrinsic("vector", 2, {floating})'
vector_value = f'@intrinsic("construct", {vector}, @intrinsic("cast", 1, {floating}), @intrinsic("cast", 2, {floating}))'
invalid = {
    "class": '@intrinsic("construct", box, 3)',
    "pointer": '"text"',
    "boolean": '@intrinsic("cast", 1, @intrinsic("type", "bool"))',
    "type": integer,
    "array": f'@intrinsic("construct", @intrinsic("array", 2, {integer}), 1, 2)',
    "matrix": '@intrinsic("construct", @intrinsic("matrix", 2, 2))',
    "vector_pointer": '@intrinsic("address of", lanes)',
}

with tempfile.TemporaryDirectory(prefix="dynlex-unary-types-") as directory:
    temporary = Path(directory)
    source = temporary / "case.dl"
    for mode in ("--emit-llvm", "--emit-wasm", "--emit-spirv"):
        for operation in operations:
            for name, operand in invalid.items():
                source.write_text(
                    class_prefix
                    + f'@intrinsic("store", lanes, {vector_value})\n'
                    + f'@intrinsic("discard", @intrinsic("{operation}", {operand}))\n'
                )
                result = subprocess.run(
                    [str(compiler), str(source), mode, "-o", str(temporary / "result")],
                    capture_output=True, text=True,
                )
                diagnostics = result.stdout + result.stderr
                assert result.returncode == 1, (mode, operation, name, result.returncode, diagnostics)
                assert f"Arithmetic operator '{operation}' requires a numeric scalar or vector operand" in diagnostics, diagnostics
        print(f"{mode}: invalid operands rejected")

    for kind in (integer, floating, '@intrinsic("type", "float", 64)', vector):
        for operation in operations:
            operand = vector_value if kind == vector else f'@intrinsic("cast", 2, {kind})'
            source.write_text(
                f'@intrinsic("store", value, {operand})\n'
                f'@intrinsic("discard", @intrinsic("{operation}", value))\n'
            )
            output = temporary / "valid.ll"
            result = subprocess.run(
                [str(compiler), str(source), "--emit-llvm", "-O0", "-o", str(output)],
                capture_output=True, text=True,
            )
            assert result.returncode == 0, (kind, operation, result.stdout + result.stderr)
            if kind == vector and operation == "negate":
                assert "fneg <2 x float>" in output.read_text(), output.read_text()
    print("Numeric scalar/vector operations passed")
