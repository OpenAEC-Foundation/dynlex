#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise compute compilation, Vulkan dispatch, and CPU/GPU plane agreement."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
TESTS = ROOT / "tests/cadkernel/gpu"
BUILD = ROOT / "build"
EXECUTABLE_SUFFIX = ".exe" if sys.platform == "win32" else ""


def run(*command: object, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(part) for part in command], cwd=ROOT, text=True, capture_output=True, check=False
    )
    if expect_success and result.returncode != 0:
        raise AssertionError(f"{' '.join(map(str, command))}\n{result.stdout}{result.stderr}")
    return result


def shader_model(path: Path) -> tuple[int, tuple[int, int, int] | None]:
    data = path.read_bytes()
    if len(data) % 4:
        raise AssertionError(f"misaligned SPIR-V: {path}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if len(words) < 5 or words[0] != 0x07230203:
        raise AssertionError(f"invalid SPIR-V header: {path}")
    model = None
    local_size = None
    position = 5
    while position < len(words):
        size = words[position] >> 16
        opcode = words[position] & 0xFFFF
        if size == 0 or position + size > len(words):
            raise AssertionError(f"invalid SPIR-V instruction: {path}")
        if opcode == 15 and size >= 4:
            model = words[position + 1]
        if opcode == 16 and size >= 6 and words[position + 2] == 17:
            local_size = tuple(words[position + 3 : position + 6])
        position += size
    if model is None:
        raise AssertionError(f"missing SPIR-V entry point: {path}")
    return model, local_size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=BUILD / f"dynlex{EXECUTABLE_SUFFIX}")
    parser.add_argument("--expect", choices=("gpu", "fallback", "either"), default="either")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    BUILD.mkdir(exist_ok=True)

    for name, stage, model in (
        ("empty_compute.dl", "compute", 5),
        ("../../games/passthrough_vertex.dl", "vertex", 0),
        ("../../games/mandelbrot_shader.dl", "fragment", 4),
    ):
        source = (TESTS / name).resolve()
        output = BUILD / ("empty-compute.spv" if stage == "compute" else f"gpu-verify-{stage}.spv")
        run(compiler, source, "--emit-spirv", f"--shader-stage={stage}", "-o", output)
        actual_model, local_size = shader_model(output)
        if actual_model != model or (stage == "compute" and local_size != (64, 1, 1)):
            raise AssertionError(f"wrong {stage} shader entry point or workgroup size")

    for name, expected in (
        ("invalid_compute_input.dl", "shader input 'fragcoord' is unavailable"),
        ("invalid_compute_output.dl", "shader operation 'shader output' is unavailable"),
        ("invalid_compute_uniform.dl", "shader operation 'shader uniform' is unavailable"),
        ("invalid_compute_interpolant_input.dl", "shader interpolant input 'sample' is only available in fragment shaders"),
        ("invalid_compute_interpolant_output.dl", "shader interpolant output 'sample' is only available in vertex shaders"),
    ):
        result = run(
            compiler, TESTS / name, "--emit-spirv", "--shader-stage=compute",
            "-o", BUILD / "gpu-verify-invalid.spv", expect_success=False,
        )
        if result.returncode != 1 or expected not in (result.stdout + result.stderr).lower():
            raise AssertionError(f"compute diagnostic for {name}: {result.stdout}{result.stderr}")

    run(sys.executable, "-B", TESTS / "generate_plane_lift.py", BUILD / "plane-lift.spv")
    model, local_size = shader_model(BUILD / "plane-lift.spv")
    if model != 5 or local_size != (64, 1, 1):
        raise AssertionError("wrong plane shader entry point or workgroup size")

    dispatch_program = BUILD / f"gpu-verify-dispatch{EXECUTABLE_SUFFIX}"
    run(compiler, TESTS / "compute_dispatch.dl", "-O2", "-o", dispatch_program)
    run(dispatch_program)

    observed = set()
    for optimization in ("O0", "O2"):
        output = BUILD / f"gpu-verify-plane-{optimization}{EXECUTABLE_SUFFIX}"
        run(compiler, TESTS / "plane_lift.dl", f"-{optimization}", "-o", output)
        result = run(output)
        if "64 planar GPU points match the CPU kernel" in result.stdout:
            observed.add("gpu")
        elif "64-bit GPU compute unavailable; dispatch rejected" in result.stdout:
            observed.add("fallback")
        else:
            raise AssertionError(f"missing plane result: {result.stdout}{result.stderr}")
    if len(observed) != 1 or (args.expect != "either" and args.expect not in observed):
        raise AssertionError(f"unexpected GPU capability path: {observed}")
    print(f"compute shaders, diagnostics, dispatch, and CPU/GPU plane: {observed.pop()} (O0, O2)")


if __name__ == "__main__":
    main()
