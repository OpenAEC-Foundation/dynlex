#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare two connected planar sheet faces against pinned Rust at O0/O2."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
THICKEN_HASH = "1b499aa9b21da2786c290e7adfa5274c04d52183638c213301748d7eb9bdca0c"
CASES = 9
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "paired_edges", "pcurves", "forward_faces",
)


def run(command: list[str | Path], timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(value: str) -> tuple[bool, bool, tuple[int, ...], list[tuple[float, float, float]]]:
    numbers = [float(item) for item in value.split()]
    if len(numbers) < 2 or any(item not in (0, 1) for item in numbers[:2]):
        raise AssertionError(f"missing validity or immutability flag: {value!r}")
    valid, unchanged = (bool(item) for item in numbers[:2])
    if not valid:
        if len(numbers) != 2:
            raise AssertionError("invalid result carried a body")
        return valid, unchanged, (), []
    if len(numbers) < 2 + len(FIELDS):
        raise AssertionError("incomplete body summary")
    fields = tuple(int(item) for item in numbers[2:2 + len(FIELDS)])
    if any(item != float(field) for item, field in zip(numbers[2:2 + len(FIELDS)], fields)):
        raise AssertionError("non-integral topology field")
    coordinates = numbers[2 + len(FIELDS):]
    if len(coordinates) != 3 * fields[0]:
        raise AssertionError("vertex count mismatch")
    points = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    points.sort(key=lambda point: (tuple(round(value, 8) for value in point), point))
    return valid, unchanged, fields, points


def near_tuple(name: str, got: tuple[float, ...], want: tuple[float, ...]) -> None:
    if len(got) != len(want):
        raise AssertionError(f"{name}: length differs")
    for index, (actual, expected) in enumerate(zip(got, want)):
        if not math.isclose(actual, expected, rel_tol=2e-10, abs_tol=1e-8):
            raise AssertionError(f"{name} field {index}: {actual} != {expected}")


def box_mass(name: str, fields: tuple[int, ...], points: list[tuple[float, float, float]]) -> tuple[float, ...]:
    if fields[9] != 1 or fields[10] != 0 or fields[11] != fields[1]:
        raise AssertionError(f"{name}: result is not one closed, valid solid: {fields}")
    lows = tuple(min(point[index] for point in points) for index in range(3))
    highs = tuple(max(point[index] for point in points) for index in range(3))
    sizes = tuple(highs[index] - lows[index] for index in range(3))
    near_tuple(f"{name} dimensions", sizes, (4.0, 0.25, 3.0))
    volume = math.prod(sizes)
    centroid = tuple((lows[index] + highs[index]) / 2 for index in range(3))
    near_tuple(f"{name} mass anchor", (volume, centroid[0], abs(centroid[1]), centroid[2]),
               (3.0, 2.0, 0.125, 1.5))
    return (volume, *centroid)


def compare(name: str, rust: str, native: str) -> None:
    expected = observe(rust)
    actual = observe(native)
    if expected[:2] != actual[:2]:
        raise AssertionError(f"{name}: validity or source integrity differs: {expected[:2]} != {actual[:2]}")
    if not expected[1]:
        raise AssertionError(f"{name}: source body changed")
    if not expected[0]:
        return
    if expected[2] != actual[2]:
        raise AssertionError(f"{name}: topology {actual[2]} != Rust {expected[2]}")
    if len(expected[3]) != len(actual[3]):
        raise AssertionError(f"{name}: vertex count differs")
    for index, (want_point, got_point) in enumerate(zip(expected[3], actual[3])):
        near_tuple(f"{name} vertex {index}", got_point, want_point)
    near_tuple(f"{name} analytic mass", box_mass(name, actual[2], actual[3]),
               box_mass(name, expected[2], expected[3]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex-expression-fix-v2.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--mode", choices=("O0", "O2"), action="append")
    parser.add_argument("--reference-only", action="store_true")
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    digest = hashlib.sha256((source / "src/brep/thicken.rs").read_bytes()).hexdigest()
    if digest != THICKEN_HASH:
        raise AssertionError(f"source hash {digest} != {THICKEN_HASH}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-thicken-multiface-planar-") as temporary:
        output = Path(temporary)
        modes = tuple(dict.fromkeys(arguments.mode or ("O0", "O2")))
        for mode in modes:
            directory = "debug" if mode == "O0" else "release"
            library = (arguments.reference_root / directory / "libcadkernel.rlib").resolve()
            dependencies = (arguments.reference_root / directory / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = output / f"reference-{mode}{suffix}"
            native_binary = output / f"native-{mode}{suffix}"
            run([arguments.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library}", "-L", f"dependency={dependencies}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            if arguments.reference_only:
                for case in range(CASES):
                    observed = observe(run([rust_binary, str(case)], timeout=30))
                    if observed[0] != (case < 6) or not observed[1]:
                        raise AssertionError(f"case-{case}/{mode}: unexpected Rust outcome {observed[:2]}")
                    if observed[0]:
                        box_mass(f"case-{case}/{mode}", observed[2], observed[3])
                    print(f"RUST case-{case}/{mode}: valid={observed[0]} unchanged={observed[1]}", flush=True)
                continue
            diagnostics = run([arguments.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            if diagnostics:
                raise AssertionError(f"DynLex diagnostics {mode}: {diagnostics}")
            for case in range(CASES):
                name = f"case-{case}/{mode}"
                compare(name, run([rust_binary, str(case)], timeout=30),
                        run([native_binary, str(case)], timeout=30))
                print(f"PASS {name}", flush=True)
    print(f"PASS: {CASES} cases at {', '.join(modes)}", flush=True)


if __name__ == "__main__":
    main()
