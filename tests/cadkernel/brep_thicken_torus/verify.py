#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare bounded toroidal-sheet thickening with pinned Rust at O0/O2."""
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
CASES = 20
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "paired_edges", "pcurves", "forward_faces",
)


def run(command: list[str | Path], timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(value: str) -> tuple[bool, bool, bool, tuple[int, ...], tuple[float, ...], list[tuple[float, float, float]]]:
    numbers = [float(item) for item in value.split()]
    if len(numbers) < 3 or any(item not in (0, 1) for item in numbers[:3]):
        raise AssertionError(f"missing validity, immutability or orientation flag: {value!r}")
    valid, unchanged, forward = (bool(item) for item in numbers[:3])
    if not valid:
        if len(numbers) != 3:
            raise AssertionError("invalid result carried a body")
        return valid, unchanged, forward, (), (), []
    if len(numbers) < 3 + len(FIELDS) + 3:
        raise AssertionError("incomplete body summary")
    fields = tuple(int(item) for item in numbers[3:3 + len(FIELDS)])
    if any(item != float(field) for item, field in zip(numbers[3:3 + len(FIELDS)], fields)):
        raise AssertionError("non-integral topology field")
    start = 3 + len(FIELDS)
    radii = tuple(numbers[start:start + 3])
    coordinates = numbers[start + 3:]
    if len(coordinates) != 3 * fields[0]:
        raise AssertionError("vertex count mismatch")
    points = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    points.sort(key=lambda point: (tuple(round(value, 8) for value in point), point))
    return valid, unchanged, forward, fields, radii, points


def angle(case: int) -> float:
    return {
        2: -math.pi / 2, 3: -math.pi / 2,
        4: 3 * math.pi / 2, 5: 3 * math.pi / 2,
        6: math.tau, 7: math.tau,
        8: 1.99 * math.pi, 9: 1.99 * math.pi,
    }.get(case, math.pi / 2)


def profile(case: int) -> tuple[float, float, float, float]:
    if case in (10, 11):
        return 5.0, 1.0, -2.5, 2.5
    if case == 15:
        return 1.0, 2.0, -0.8, 0.9
    if case in (18, 19):
        return 5.0, 1.0, 0.9, math.tau - 0.8
    return 5.0, 1.0, -0.8, 0.9


def analytic_mass(major: float, inner: float, outer: float, u0: float, u1: float,
                  v0: float, v1: float) -> tuple[float, ...]:
    low_u, high_u = sorted((u0, u1))
    low_v, high_v = sorted((v0, v1))
    delta_u, delta_v = high_u - low_u, high_v - low_v
    radial2 = (outer**2 - inner**2) / 2
    radial3 = (outer**3 - inner**3) / 3
    radial4 = (outer**4 - inner**4) / 4
    delta_sin = math.sin(high_v) - math.sin(low_v)
    cosine_square = delta_v / 2 + (math.sin(2 * high_v) - math.sin(2 * low_v)) / 4
    volume = delta_u * (major * radial2 * delta_v + radial3 * delta_sin)
    around_second = major**2 * radial2 * delta_v + 2 * major * radial3 * delta_sin + radial4 * cosine_square
    axial = delta_u * (major * radial3 * (math.cos(low_v) - math.cos(high_v))
                       + radial4 * (math.sin(high_v)**2 - math.sin(low_v)**2) / 2)
    return (volume,
            (math.sin(high_u) - math.sin(low_u)) * around_second / volume,
            (math.cos(low_u) - math.cos(high_u)) * around_second / volume,
            axial / volume)


def near_tuple(name: str, got: tuple[float, ...], want: tuple[float, ...]) -> None:
    if len(got) != len(want):
        raise AssertionError(f"{name}: length differs")
    for index, (actual, expected) in enumerate(zip(got, want)):
        if not math.isclose(actual, expected, rel_tol=2e-10, abs_tol=1e-8):
            raise AssertionError(f"{name} field {index}: {actual} != {expected}")


def compare(name: str, case: int, rust: str, native: str) -> None:
    expected = observe(rust)
    actual = observe(native)
    if expected[:3] != actual[:3]:
        raise AssertionError(f"{name}: validity, source integrity or orientation differs: {expected[:3]} != {actual[:3]}")
    if not expected[1]:
        raise AssertionError(f"{name}: source body changed")
    if not expected[0]:
        return
    if expected[3] != actual[3]:
        raise AssertionError(f"{name}: topology {actual[3]} != Rust {expected[3]}")
    near_tuple(f"{name} radii", actual[4], expected[4])
    if len(expected[5]) != len(actual[5]):
        raise AssertionError(f"{name}: vertex count differs")
    for index, (want_point, got_point) in enumerate(zip(expected[5], actual[5])):
        near_tuple(f"{name} vertex {index}", got_point, want_point)
    major, minor, v0, v1 = profile(case)
    given_distance = -0.25 if case == 19 or (case <= 11 and case % 2 == 1) else 0.25
    oriented = given_distance if expected[2] else -given_distance
    wanted_radii = (major, min(minor, minor + oriented), max(minor, minor + oriented))
    near_tuple(f"{name} expected radii", expected[4], wanted_radii)
    reference_mass = analytic_mass(*expected[4], 0.0, angle(case), v0, v1)
    native_mass = analytic_mass(*actual[4], 0.0, angle(case), v0, v1)
    wanted_mass = analytic_mass(*wanted_radii, 0.0, angle(case), v0, v1)
    near_tuple(f"{name} mass", native_mass, reference_mass)
    near_tuple(f"{name} analytic mass", native_mass, wanted_mass)


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
    with tempfile.TemporaryDirectory(prefix="cad-thicken-torus-") as temporary:
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
                    print(f"RUST case-{case}/{mode}: valid={observed[0]} unchanged={observed[1]} forward={observed[2]}", flush=True)
                continue
            diagnostics = run([arguments.compiler, HERE / "probe.dl", f"-{mode}", "-o", native_binary])
            if diagnostics:
                raise AssertionError(f"DynLex diagnostics {mode}: {diagnostics}")
            for case in range(CASES):
                name = f"case-{case}/{mode}"
                compare(name, case, run([rust_binary, str(case)], timeout=30),
                        run([native_binary, str(case)], timeout=30))
                print(f"PASS {name}", flush=True)
    print(f"PASS: {CASES} cases at {', '.join(modes)}", flush=True)


if __name__ == "__main__":
    main()
