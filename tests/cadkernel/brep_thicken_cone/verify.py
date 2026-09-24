#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare bounded conical-sheet thickening with pinned Rust at O0/O2."""
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
CASES = 19
FIELDS = (
    "vertices", "edges", "coedges", "loops", "faces", "shells", "lumps",
    "surfaces", "curves", "roots", "flaws", "paired_edges", "pcurves", "forward_faces",
)


def run(command: list[str | Path], timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(value: str) -> tuple[bool, bool, bool, tuple[int, ...], list[tuple[float, float, float]]]:
    numbers = [float(item) for item in value.split()]
    if len(numbers) < 3 or any(item not in (0, 1) for item in numbers[:3]):
        raise AssertionError(f"missing validity, immutability or orientation flag: {value!r}")
    valid, unchanged, forward = (bool(item) for item in numbers[:3])
    if not valid:
        if len(numbers) != 3:
            raise AssertionError("invalid result carried a body")
        return valid, unchanged, forward, (), []
    if len(numbers) < 3 + len(FIELDS):
        raise AssertionError("incomplete body summary")
    fields = tuple(int(item) for item in numbers[3:3 + len(FIELDS)])
    if any(item != float(field) for item, field in zip(numbers[3:3 + len(FIELDS)], fields)):
        raise AssertionError("non-integral topology field")
    coordinates = numbers[3 + len(FIELDS):]
    if len(coordinates) != 3 * fields[0]:
        raise AssertionError("vertex count mismatch")
    points = [tuple(coordinates[index:index + 3]) for index in range(0, len(coordinates), 3)]
    points.sort(key=lambda point: (tuple(round(value, 8) for value in point), point))
    return valid, unchanged, forward, fields, points


def profile(case: int) -> tuple[tuple[float, float], tuple[float, float]]:
    if case in (10, 11):
        return (2.0, 0.0), (4.0, 5.0)
    if case == 16:
        return (4.0, 0.0), (0.0, 5.0)
    if case in (17, 18):
        return (2.0, 5.0), (4.0, 0.0)
    return (4.0, 0.0), (2.0, 5.0)


def angle(case: int) -> float:
    return {
        2: -math.pi / 2, 3: -math.pi / 2,
        4: 3 * math.pi / 2, 5: 3 * math.pi / 2,
        6: math.tau, 7: math.tau,
        8: 1.99 * math.pi, 9: 1.99 * math.pi,
    }.get(case, math.pi / 2)


def mass_from_corners(corners: list[tuple[float, float]], sweep: float) -> tuple[float, ...]:
    if len(corners) != 4:
        raise AssertionError(f"expected four distinct conical profile corners, got {corners}")
    centre_r = sum(point[0] for point in corners) / 4
    centre_z = sum(point[1] for point in corners) / 4
    corners.sort(key=lambda point: math.atan2(point[1] - centre_z, point[0] - centre_r))
    first_moment = second_moment = axial_moment = 0.0
    for index, (radius, height) in enumerate(corners):
        next_radius, next_height = corners[(index + 1) % 4]
        cross = radius * next_height - next_radius * height
        first_moment += cross * (radius + next_radius) / 6
        second_moment += cross * (radius * radius + radius * next_radius + next_radius * next_radius) / 12
        axial_moment += cross * (2 * radius * height + radius * next_height
                                 + next_radius * height + 2 * next_radius * next_height) / 24
    if first_moment <= 0:
        raise AssertionError(f"invalid revolved profile moment: {first_moment}")
    low, high = min(0.0, sweep), max(0.0, sweep)
    volume = (high - low) * first_moment
    return (volume, (math.sin(high) - math.sin(low)) * second_moment / volume,
            (math.cos(low) - math.cos(high)) * second_moment / volume,
            (high - low) * axial_moment / volume)


def mass_from_vertices(points: list[tuple[float, float, float]], sweep: float) -> tuple[float, ...]:
    corners: list[tuple[float, float]] = []
    for x, y, z in points:
        candidate = math.hypot(x, y), z
        if not any(math.dist(candidate, existing) < 1e-8 for existing in corners):
            corners.append(candidate)
    return mass_from_corners(corners, sweep)


def expected_mass(case: int, forward: bool) -> tuple[float, ...]:
    (radius0, height0), (radius1, height1) = profile(case)
    given_distance = 0.5 if case == 17 or (case != 18 and case % 2 == 0) else -0.5
    distance = given_distance if forward else -given_distance
    slope = (radius0 - radius1) / (height1 - height0)
    cosine = 1 / math.sqrt(1 + slope * slope)
    sine = slope * cosine
    corners = [(radius0, height0), (radius1, height1),
               (radius1 + distance * cosine, height1 + distance * sine),
               (radius0 + distance * cosine, height0 + distance * sine)]
    return mass_from_corners(corners, angle(case))


def near_tuple(name: str, got: tuple[float, ...], want: tuple[float, ...]) -> None:
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
    if len(expected[4]) != len(actual[4]):
        raise AssertionError(f"{name}: vertex count differs")
    for index, (want_point, got_point) in enumerate(zip(expected[4], actual[4])):
        near_tuple(f"{name} vertex {index}", got_point, want_point)
    reference_mass = mass_from_vertices(expected[4], angle(case))
    native_mass = mass_from_vertices(actual[4], angle(case))
    near_tuple(f"{name} mass", native_mass, reference_mass)
    near_tuple(f"{name} analytic mass", native_mass, expected_mass(case, expected[2]))


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
    with tempfile.TemporaryDirectory(prefix="cad-thicken-cone-") as temporary:
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
