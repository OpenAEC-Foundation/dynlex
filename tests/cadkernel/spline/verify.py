# SPDX-License-Identifier: MPL-2.0
"""Verify the spline port against the pinned, unmodified Rust module.

From the repository root:
    python tests/cadkernel/spline/verify.py --source /path/to/cadkernel
Requires rustc and a built build/dynlex (build/dynlex.exe on Windows).
Generated drivers and executables stay under build/cadkernel-spline-checks.
The N=0 interpolation check runs last and verifies zero-sized list elements
through both open and periodic interpolation.
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


def run(command, timeout=60):
    started = time.perf_counter()
    with unattended_child_processes():
        result = subprocess.run(
            [str(part) for part in command], cwd=ROOT,
            capture_output=True, text=True, timeout=timeout,
        )
    if result.returncode:
        raise AssertionError(
            f"Command failed ({result.returncode}): {command}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    if result.stderr:
        print(result.stderr, end="", flush=True)
    return result.stdout, time.perf_counter() - started


def build(command):
    stdout, elapsed = run(command)
    print(stdout, end="")
    print(f"compile {pathlib.Path(command[-1]).name}: {elapsed:.3f}s", flush=True)


def parse(text):
    if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", text, re.IGNORECASE):
        return math.nan
    return float(text)


def equivalent(actual, expected, exact=False):
    if math.isnan(expected):
        return math.isnan(actual)
    if actual == expected:
        return actual != 0 or math.copysign(1, actual) == math.copysign(1, expected)
    if math.isinf(actual) or math.isinf(expected):
        return False
    return not exact and math.isclose(actual, expected, rel_tol=3e-13, abs_tol=1e-300)


def clamped(degree, count):
    spans = max(0, count - degree)
    return [0.0] * (degree + 1) + [i / spans for i in range(1, spans)] + [1.0] * (degree + 1)


def pack(dimension, operation, points, *, degree=3, knots=(), spacing=0,
         start=None, end=None, u=0.5, count=None):
    if count is None:
        count = len(points)
    return [
        dimension, operation, degree, count, len(knots), spacing,
        int(start is not None), int(end is not None), u,
        *knots, *(x for point in points for x in point),
        *(start if start is not None else [0.0] * dimension),
        *(end if end is not None else [0.0] * dimension),
    ]


def cases():
    rng = random.Random(953546)
    # Includes the dimension absent from fixed vector2/vector3 APIs.
    for dimension in (0, 1, 2, 3, 4, 5, 8, 17):
        for degree in (0, 1, 2, 3, 15, 16, 20):
            count = degree + 3
            points = [[rng.uniform(-8, 8) for _ in range(dimension)] for _ in range(count)]
            knots = clamped(degree, count)
            for u in (-0.25, 0.0, 0.5, 1.0, 1.25):
                yield f"deBoor N={dimension} degree={degree} u={u}", pack(
                    dimension, 0, points, degree=degree, knots=knots, u=u)
        yield f"empty N={dimension}", pack(dimension, 0, [], degree=3, knots=[], u=math.nan)
    for degree, count in ((0, 0), (0, 3), (3, 0), (3, 2), (3, 4), (3, 9), (20, 25)):
        yield f"clamped degree={degree} count={count}", pack(
            1, 3, [[0.0]] * count, degree=degree)
    for upper in (0.0, 1e-16, 1e-15, 1.0000000000000003e-15, math.nan, math.inf):
        yield f"denominator {upper}", pack(
            1, 0, [[2.0], [9.0]], degree=1, knots=[0.0, 0.0, upper, upper], u=upper)
    for u in (math.nan, math.inf, -math.inf):
        for degree in (0, 3):
            yield f"nonfinite parameter degree={degree} u={u}", pack(
                2, 0, [[0.0, 0.0], [1.0, 4.0], [5.0, 4.0], [6.0, 0.0]],
                degree=degree, knots=clamped(degree, 4), u=u)
    yield "repeated internal knots", pack(
        2, 0, [[i * 1.0, (-1.0) ** i] for i in range(7)],
        degree=3, knots=[0.0] * 4 + [0.5] * 3 + [1.0] * 4, u=0.5)
    for dimension in (1, 2, 3, 4, 5, 8, 17):
        for spacing in (0, 1, 2):
            for count in (0, 1, 2, 3, 6):
                points = [[rng.uniform(-8, 8) for _ in range(dimension)] for _ in range(count)]
                for operation in (1, 2):
                    yield f"interpolation op={operation} N={dimension} spacing={spacing} count={count}", pack(
                        dimension, operation, points, spacing=spacing)
                if count >= 2:
                    start = [rng.uniform(-3, 3) for _ in range(dimension)]
                    end = [rng.uniform(-3, 3) for _ in range(dimension)]
                    for first, last in ((start, None), (None, end), (start, end)):
                        yield f"tangents N={dimension} spacing={spacing} count={count}", pack(
                            dimension, 1, points, spacing=spacing, start=first, end=last)
    for spacing in (0, 1, 2):
        square = [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]]
        for closing in (0.0, 5e-10, 1e-9, 2e-9):
            yield f"closing threshold spacing={spacing} closing={closing}", pack(
                2, 2, square + [[closing, 0.0]], spacing=spacing)
        for count in (2, 3, 4):
            for operation in (1, 2):
                yield f"coincident spacing={spacing} count={count} op={operation}", pack(
                    2, operation, [[2.0, 3.0]] * count, spacing=spacing)
        for value in (1e-200, 1e-10, 1e150, 1e200, 1e308, math.nan, math.inf, -math.inf):
            for operation in (1, 2):
                yield f"extreme value={value} spacing={spacing} op={operation}", pack(
                    2, operation, [[0.0, 0.0], [value, 1.0], [0.0, 2.0]], spacing=spacing)
        for tangent in (0.0, 1e-10, 1e-9, 1.0000000000000003e-9, math.nan, math.inf):
            yield f"tangent threshold value={tangent} spacing={spacing}", pack(
                1, 1, [[0.0], [1.0], [0.0]], spacing=spacing, start=[tangent])
    yield "survey coordinates", pack(
        3, 1, [[512345.678, 4512345.678, 91.5],
               [512345.679, 4512345.678, 91.5],
               [512345.680, 4512345.678, 91.501]], spacing=2)


def compare(case, name, executables):
    arguments = [repr(float(value)) for value in case]
    rows = []
    for executable in executables:
        stdout, _ = run([executable, *arguments], timeout=10)
        rows.append([parse(line) for line in stdout.splitlines()])
    reference, plain, optimized = rows
    if len(reference) != len(plain) or len(reference) != len(optimized):
        raise AssertionError(f"{name}: result lengths differ: {[len(row) for row in rows]}")
    for index, (want, first, second) in enumerate(zip(*rows)):
        for level, actual in (("O0", first), ("O2", second)):
            if not equivalent(actual, want):
                raise AssertionError(
                    f"{name}, output {index}, {level}: {actual!r} != Rust {want!r}; input={case}"
                )
        if not equivalent(first, second, exact=True):
            raise AssertionError(f"{name}, output {index}: O0 {first!r} != O2 {second!r}")
    return len(reference)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument(
        "--compiler", type=pathlib.Path,
        default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"),
    )
    args = parser.parse_args()
    args.compiler = args.compiler.resolve()
    source = args.source.resolve()
    rustc = shutil.which("rustc")
    if rustc is None:
        raise RuntimeError("rustc is required")
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    revision, _ = run([*git, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError(f"Expected upstream {PIN}, found {revision.strip()}")
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/space/spline.rs"])
    revision, _ = run(["git", "rev-parse", "HEAD"])
    print(f"source revision: {PIN}; compiler checkout: {revision.strip()}", flush=True)
    directory = ROOT / "build/cadkernel-spline-checks"
    directory.mkdir(parents=True, exist_ok=True)
    probes = []
    expected = (ROOT / "tests/required/cadkernel_spline/expected.txt").read_text()
    for level in ("O0", "O2"):
        executable = directory / f"fixtures-{level}.out"
        build([args.compiler, "tests/required/cadkernel_spline/main.dl", f"-{level}", "-o", executable])
        stdout, _ = run([executable])
        if stdout != expected:
            raise AssertionError(f"{level} independent fixtures mismatch:\n{stdout}")
        print(f"{level}: independent fixtures passed", flush=True)
        probe = directory / f"probe-{level}.out"
        build([args.compiler, "tests/cadkernel/spline/probe.dl", f"-{level}", "-o", probe])
        probes.append(probe)
    rejected = subprocess.run(
        [str(args.compiler), "tests/cadkernel/spline/wrong_tangent_dimension.dl",
         "-O2", "-o", str(directory / "wrong-tangent.out")],
        cwd=ROOT, capture_output=True, text=True, timeout=60,
    )
    if rejected.returncode == 0 or "No overload matches call 'check cad spline shape" not in (rejected.stdout + rejected.stderr):
        raise AssertionError(f"Expected tangent dimension rejection:\n{rejected.stdout}\n{rejected.stderr}")
    print("mismatched tangent dimension rejected", flush=True)
    upstream_tests = directory / "upstream-tests.out"
    build([rustc, "--edition=2021", "--test", source / "src/space/spline.rs", "-O", "-o", upstream_tests])
    stdout, _ = run([upstream_tests])
    print(stdout, end="", flush=True)
    driver = directory / "reference-driver.rs"
    module_path = (source / "src/space/spline.rs").as_posix()
    helper_path = (ROOT / "tests/cadkernel/spline/reference.rs").as_posix()
    driver.write_text(
        f'#[path = r"{module_path}"]\nmod spline;\ninclude!(r"{helper_path}");\n',
        encoding="utf-8",
    )
    reference = directory / "reference.out"
    build([rustc, "--edition=2021", "--crate-name", "spline_reference", driver, "-O", "-o", reference])
    total = fields = 0
    executables = (reference, *probes)
    for name, case in cases():
        fields += compare(case, name, executables)
        total += 1
        if total % 200 == 0:
            print(f"runtime differential: {total} cases checked", flush=True)
    print(
        f"runtime differential: {total} cases, {fields * 2} comparisons against Rust passed; "
        "O0/O2 exact numerical parity (including zero sign and NaN classification)",
        flush=True,
    )
    # Keep this supported upstream case in addition to the allocation regression.
    for operation in (1, 2):
        compare(pack(0, operation, [[], [], [], []]), f"N=0 interpolation op={operation}", executables)
    print("N=0 open and periodic interpolation passed", flush=True)


if __name__ == "__main__":
    with unattended_child_processes():
        main()
