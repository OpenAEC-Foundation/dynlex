#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare the pinned rectangular line sweep with the DynLex miter port."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SWEEP_HASH = "492ac57683c6681f6e84f9fbbbdee2001b2b18a2bd766eb714588f90e4ffb995"
CASES = ("right-angle", "oblique", "collinear", "reversed-pieces", "single-line", "two-bends")
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "two-coedge-edges", "pcurves",
    "forward-faces",
)


def run(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(output: str) -> tuple[tuple[int, ...], list[tuple[float, float, float]]]:
    numbers = [float(value) for value in output.split()]
    if len(numbers) < 1 + len(FIELDS) or numbers[0] != 1:
        raise AssertionError(f"expected a valid solid: {output!r}")
    fields = tuple(int(value) for value in numbers[1:1 + len(FIELDS)])
    if any(value != float(int(value)) for value in numbers[1:1 + len(FIELDS)]):
        raise AssertionError("non-integral arena statistic")
    coordinates = numbers[1 + len(FIELDS):]
    if len(coordinates) != 3 * fields[0] or not all(map(math.isfinite, coordinates)):
        raise AssertionError("incomplete or nonfinite vertices")
    vertices = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    return fields, sorted(vertices, key=lambda point: (tuple(round(value, 8) for value in point), point))


def compare(name: str, source: str, native: str) -> int:
    source_fields, source_vertices = observe(source)
    native_fields, native_vertices = observe(native)
    for field, expected, actual in zip(FIELDS, source_fields, native_fields):
        if expected != actual:
            raise AssertionError(f"{name}: {field}: Rust={expected}, DynLex={actual}")
    for index, (expected, actual) in enumerate(zip(source_vertices, native_vertices)):
        for axis, (left, right) in enumerate(zip(expected, actual)):
            if not math.isclose(left, right, rel_tol=1e-10, abs_tol=1e-8):
                raise AssertionError(
                    f"{name}: vertex {index} axis {axis}: Rust={left}, DynLex={right}"
                )
    return len(FIELDS) + 3 * len(source_vertices)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_hash = hashlib.sha256((source / "src/brep/sweep.rs").read_bytes()).hexdigest()
    if source_hash != SWEEP_HASH:
        raise AssertionError(f"source hash {source_hash} != {SWEEP_HASH}")

    suffix = ".exe" if sys.platform == "win32" else ".out"
    comparisons = 0
    with tempfile.TemporaryDirectory(prefix="cad-miter-diff-") as directory:
        temporary = Path(directory)
        for mode, rust_directory in (("O0", "debug"), ("O2", "release")):
            library = (arguments.reference_root / rust_directory / "libcadkernel.rlib").resolve()
            dependencies = (arguments.reference_root / rust_directory / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = temporary / f"reference-{mode}{suffix}"
            native_binary = temporary / f"native-{mode}{suffix}"
            run([
                arguments.rustc, "--edition=2021", HERE / "reference.rs",
                "--extern", f"cadkernel={library}", "-L", f"dependency={dependencies}",
                "-C", f"opt-level={mode[1]}", "-o", rust_binary,
            ])
            diagnostics = run([arguments.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            if diagnostics:
                raise AssertionError(f"{mode} DynLex diagnostics:\n{diagnostics}")
            for index, name in enumerate(CASES):
                label = f"{name}/{mode}"
                comparisons += compare(
                    label, run([rust_binary, str(index)], timeout=30),
                    run([native_binary, str(index)], timeout=30),
                )
                print(f"PASS {label}", flush=True)
    print(f"PASS: {len(CASES)} cases, {comparisons} field comparisons at O0/O2; pin={PIN}")


if __name__ == "__main__":
    main()
