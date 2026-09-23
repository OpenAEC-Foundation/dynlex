# SPDX-License-Identifier: MPL-2.0
"""Check independent fixtures, upstream tests and runtime numerical parity.

Run from any directory with Python 3 and rustc available. Build artifacts are
kept in build/cadkernel-core-checks. No Cargo dependencies or network are needed.
The source checkout must be the pinned revision, supplied using --source.
"""
import argparse
import math
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
FIELDS = [
    "sum2.x", "sum2.y", "difference2.x", "difference2.y",
    "scale2.x", "scale2.y", "divide2.x", "divide2.y",
    "negative2.x", "negative2.y", "dot2", "cross2",
    "squared_length2", "length2", "distance2", "squared_distance2",
    "lerp2.x", "lerp2.y", "segment2", "perpendicular2.x",
    "perpendicular2.y", "angle2", "normalization2.valid", "normalization2.x",
    "normalization2.y", "sum3.x", "sum3.y", "sum3.z",
    "difference3.x", "difference3.y", "difference3.z", "scale3.x",
    "scale3.y", "scale3.z", "divide3.x", "divide3.y",
    "divide3.z", "negative3.x", "negative3.y", "negative3.z",
    "dot3", "cross3.x", "cross3.y", "cross3.z",
    "squared_length3", "length3", "distance3", "squared_distance3",
    "lerp3.x", "lerp3.y", "lerp3.z", "segment3",
    "normalization3.valid", "normalization3.x", "normalization3.y", "normalization3.z",
    "finite3", "parallel3",
]
def run(command):
    started = time.perf_counter()
    with unattended_child_processes():
        result = subprocess.run(
            [str(part) for part in command], cwd=ROOT, capture_output=True,
            text=True, timeout=60,
        )
    elapsed = time.perf_counter() - started
    if result.returncode:
        raise AssertionError(
            f"Command failed ({result.returncode}): {command}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    if result.stderr:
        print(result.stderr, end="")
    return result.stdout, elapsed


def compile_program(command):
    output, elapsed = run(command)
    if output:
        print(output, end="")
    print(f"compile {pathlib.Path(command[-1]).name}: {elapsed:.3f}s")


def same_number(actual, expected, *, exact=False):
    if math.isnan(expected):
        return math.isnan(actual)
    if actual == expected:
        return actual != 0 or math.copysign(1, actual) == math.copysign(1, expected)
    if math.isinf(actual) or math.isinf(expected):
        return False
    return not exact and math.isclose(actual, expected, rel_tol=2e-14, abs_tol=1e-300)


def parse_number(text):
    # The Windows C runtime spells some NaNs "-nan(ind)".
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", text, re.IGNORECASE):
        return math.nan
    return float(text)


def cases():
    # ax, ay, az, bx, by, bz, px, py, pz, parameter/tolerance.
    # Includes Pythagorean geometry, both segment clamps, threshold boundaries,
    # orientation reversals, nonfinite data, signed zeros and f64 extremes.
    yield (1, 2, 3, 10, 20, 30, 4, 3, 8, 0.5)
    yield (0, 0, 0, 10, 0, 0, 14, 0, 3, 1)
    yield (0, 0, 0, 10, 0, 0, -4, 0, 3, -1)
    yield (10, 0, 0, 0, 0, 0, 4, 0, 3, 2)
    yield (3, 4, 0, 3, 4, 0, 0, 0, 0, 0)
    yield (0, 0, 0, 5e-13, 0, 0, 5e-13, 0, 0, 0.5)
    yield (0, 0, 0, 1e-12, 0, 0, 1e-12, 0, 0, 0.5)
    yield (512345.678, 4512345.678, 91.5, 512345.679, 4512345.678, 91.5, 0, 0, 0, 0.5)
    yield (1, 0, 0, 0, 1, 0, 0, 0, 1, 1)
    yield (1, 2, 3, -1, -2, -3, 4, 5, 6, 1e-12)
    yield (1, 2, 3, 1e6, 2e6, 3e6, 4, 5, 6, 1e-12)
    for small in (1e-150, 1e-160, 1e-162, 1e-200, 1e-300, 5e-324):
        yield (small, 0, 0, 0, small, 0, 0, 0, small, 1)
    for x in (0.0, -0.0):
        for y in (0.0, -0.0):
            yield (x, y, -0.0, -1, 0, 1, 0, 0, 0, -0.0)
    for extreme in (1e308, 1.7976931348623157e308, math.inf, -math.inf, math.nan):
        for component in range(3):
            a = [1.0, 2.0, 3.0]
            a[component] = extreme
            yield (*a, 3, 4, 5, 7, 8, 9, 0.5)
    for parameter in (math.nan, math.inf, -math.inf):
        yield (1, 0, 0, 0, 1, 0, 4, 5, 6, parameter)
    rng = random.Random(953546)
    for _ in range(96):
        scale = rng.choice((1e-150, 1e-12, 1.0, 1e6, 1e100, 1e150))
        points = [rng.uniform(-8, 8) * scale for _ in range(9)]
        yield (*points, rng.choice((-2.0, -0.25, 0.0, 0.5, 1.0, 2.0)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True, help="checkout of the pinned cadkernel revision")
    parser.add_argument("--compiler", type=pathlib.Path, default=ROOT / ("build/dynlex.exe" if os.name == "nt" else "build/dynlex"))
    args = parser.parse_args()
    source = args.source.resolve()
    rustc = shutil.which("rustc")
    if rustc is None:
        raise RuntimeError("rustc is required")
    revision, _ = run(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError(f"Expected source revision {PIN}, found {revision.strip()}")
    compiler_revision, _ = run(["git", "rev-parse", "HEAD"])
    print(f"source revision: {PIN}")
    print(f"compiler checkout revision: {compiler_revision.strip()}")
    build = ROOT / "build/cadkernel-core-checks"
    build.mkdir(parents=True, exist_ok=True)
    expected = (ROOT / "tests/required/cadkernel_core/expected.txt").read_text()
    probes = []
    for level in ("O0", "O2"):
        fixture = build / f"fixtures-{level}.out"
        compile_program([
            args.compiler, "tests/required/cadkernel_core/main.dl",
            f"-{level}", "-o", fixture,
        ])
        stdout, _ = run([fixture])
        if stdout != expected:
            raise AssertionError(f"{level} independent fixtures failed:\n{stdout}")
        print(f"{level}: independent fixtures passed")
        probe = build / f"probe-{level}.out"
        compile_program([
            args.compiler, "tests/cadkernel/core/probe.dl",
            f"-{level}", "-o", probe,
        ])
        probes.append(probe)
    libraries = []
    for dimension, module in ((2, "geom2d"), (3, "space")):
        source_file = source / "src" / module / "vec.rs"
        upstream_tests = build / f"upstream-vector{dimension}.exe"
        compile_program([rustc, "--test", source_file, "-O", "-o", upstream_tests])
        stdout, _ = run([upstream_tests])
        print(stdout.strip())
        library = build / f"libcadkernel_vector{dimension}.rlib"
        compile_program([
            rustc, "--crate-type", "rlib", "--crate-name",
            f"cadkernel_vector{dimension}", source_file, "-O", "-o", library,
        ])
        libraries.append(library)
    reference = build / "reference.exe"
    compile_program([
        rustc, "tests/cadkernel/core/reference.rs", "-O",
        "--extern", f"cadkernel_vector2={libraries[0]}",
        "--extern", f"cadkernel_vector3={libraries[1]}", "-o", reference,
    ])
    total = 0
    for index, case in enumerate(cases()):
        arguments = [repr(float(value)) for value in case]
        rows = []
        for executable in (reference, *probes):
            stdout, _ = run([executable, *arguments])
            values = [parse_number(line) for line in stdout.splitlines()]
            if len(values) != len(FIELDS):
                raise AssertionError(f"{executable}: expected {len(FIELDS)} fields, got {stdout}")
            rows.append(values)
        for column, field in enumerate(FIELDS):
            rust, plain, optimized = [row[column] for row in rows]
            for label, value in (("O0", plain), ("O2", optimized)):
                if not same_number(value, rust):
                    raise AssertionError(
                        f"case {index}, {field}, {label}: {value!r} != Rust {rust!r}; input={case}"
                    )
            if not same_number(plain, optimized, exact=True):
                raise AssertionError(
                    f"optimization parity: case {index}, {field}: {plain!r} != {optimized!r}"
                )
        total += 1
    print(
        f"runtime differential: {total} cases x {len(FIELDS)} fields x 2 optimization levels passed; "
        "O0/O2 exact numerical parity (including zero sign and NaN classification)"
    )


if __name__ == "__main__":
    main()
