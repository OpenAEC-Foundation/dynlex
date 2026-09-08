#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


UNARY_INTRINSICS = ("sin", "cos", "sqrt", "abs", "floor", "ceil", "round", "exp", "log")


def f32(value: str) -> str:
    return f'@intrinsic("cast", {value}, @intrinsic("type", "float", 32))'


def compile_llvm(compiler: Path, source: str, output: Path) -> str:
    source_path = output.with_suffix(".dl")
    source_path.write_text(source, encoding="utf-8")
    result = subprocess.run(
        [str(compiler), str(source_path), "--emit-llvm", "-o", str(output)], text=True, capture_output=True, check=False
    )
    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)
    return output.read_text(encoding="utf-8")


def discarded(expression: str) -> str:
    return f'@intrinsic("discard", {expression})\n'


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_float_math_codegen.py <compiler>", file=sys.stderr)
        return 2
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        print(f"compiler not found: {compiler}", file=sys.stderr)
        return 2

    try:
        with tempfile.TemporaryDirectory(prefix="dynlex-float-math-") as temporary_directory:
            temporary = Path(temporary_directory)
            for intrinsic in UNARY_INTRINSICS:
                llvm_ir = compile_llvm(compiler, discarded(f'@intrinsic("{intrinsic}", {f32("1.25")})'), temporary / intrinsic)
                if f"@llvm.{intrinsic if intrinsic != 'abs' else 'fabs'}.f32" not in llvm_ir:
                    raise RuntimeError(f"{intrinsic} did not use an f32 LLVM intrinsic:\n{llvm_ir}")
                if "fpext" in llvm_ir or "fptrunc" in llvm_ir:
                    raise RuntimeError(f"{intrinsic} changed f32 precision:\n{llvm_ir}")

            f32_power = compile_llvm(
                compiler, discarded(f'@intrinsic("pow", {f32("1.25")}, {f32("2.0")})'), temporary / "power-f32"
            )
            if "@llvm.pow.f32" not in f32_power or "fpext" in f32_power or "fptrunc" in f32_power:
                raise RuntimeError(f"pow did not preserve f32 precision:\n{f32_power}")

            mixed_power = compile_llvm(
                compiler, discarded(f'@intrinsic("pow", {f32("1.25")}, 2.0)'), temporary / "power-mixed"
            )
            if "@llvm.pow.f64" not in mixed_power or "fptrunc" in mixed_power:
                raise RuntimeError(f"mixed f32/f64 pow did not retain promoted f64 precision:\n{mixed_power}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
