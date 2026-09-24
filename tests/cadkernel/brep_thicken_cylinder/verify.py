#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare bounded cylindrical-sheet thickening with pinned Rust at O0/O2."""
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
THICKEN_HASH = "1b499aa9b21da2786c290e7adfa5274c04d52183638c213301748d7eb9bdca0c"
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "paired_edges", "pcurves", "forward_faces",
)


def run(command: list[str | Path], timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(value: str) -> tuple[bool, tuple[int, ...], tuple[float, ...], list[tuple[float, float, float]]]:
    numbers = [float(item) for item in value.split()]
    if not numbers or numbers[0] not in (0, 1):
        raise AssertionError(f"missing validity flag: {value!r}")
    if numbers[0] == 0:
        if len(numbers) != 1:
            raise AssertionError("invalid result carried a body")
        return False, (), (), []
    if len(numbers) < 1 + len(FIELDS) + 4:
        raise AssertionError("incomplete body summary")
    fields = tuple(int(item) for item in numbers[1:1 + len(FIELDS)])
    if any(item != float(field) for item, field in zip(numbers[1:1 + len(FIELDS)], fields)):
        raise AssertionError("non-integral topology field")
    start = 1 + len(FIELDS)
    mass = tuple(numbers[start:start + 4])
    coordinates = numbers[start + 4:]
    if len(coordinates) != 3 * fields[0]:
        raise AssertionError("vertex count mismatch")
    points = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    points.sort(key=lambda point: (tuple(round(value, 8) for value in point), point))
    return True, fields, mass, points


def compare(name: str, rust: str, native: str) -> None:
    expected = observe(rust)
    actual = observe(native)
    if expected[0] != actual[0]:
        raise AssertionError(f"{name}: validity differs")
    if not expected[0]:
        return
    if expected[1] != actual[1]:
        raise AssertionError(f"{name}: topology {actual[1]} != Rust {expected[1]}")
    if len(expected[3]) != len(actual[3]):
        raise AssertionError(f"{name}: vertex count differs")
    for field, (want, got) in enumerate(zip(expected[2], actual[2])):
        if not math.isclose(want, got, rel_tol=2e-10, abs_tol=1e-8):
            raise AssertionError(f"{name}: mass field {field}: {got} != Rust {want}")
    for index, (want_point, got_point) in enumerate(zip(expected[3], actual[3])):
        for coordinate, (want, got) in enumerate(zip(want_point, got_point)):
            if not math.isclose(want, got, rel_tol=2e-10, abs_tol=1e-8):
                raise AssertionError(f"{name}: vertex {index}/{coordinate}: {got} != Rust {want}")


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
    digest = hashlib.sha256((source / "src/brep/thicken.rs").read_bytes()).hexdigest()
    if digest != THICKEN_HASH:
        raise AssertionError(f"source hash {digest} != {THICKEN_HASH}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-thicken-cylinder-") as temporary:
        output = Path(temporary)
        for mode, directory in (("O0", "debug"), ("O2", "release")):
            library = (arguments.reference_root / directory / "libcadkernel.rlib").resolve()
            dependencies = (arguments.reference_root / directory / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            run([arguments.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library}", "-L", f"dependency={dependencies}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            diagnostics = run([arguments.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            if diagnostics:
                raise AssertionError(f"DynLex diagnostics {mode}: {diagnostics}")
            for case in range(12):
                name = f"case-{case}/{mode}"
                compare(name, run([rust_binary, str(case)], timeout=30),
                        run([native_binary, str(case)], timeout=30))
                print(f"PASS {name}", flush=True)
    print("PASS: 12 cases at O0/O2", flush=True)


if __name__ == "__main__":
    main()
