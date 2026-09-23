#!/usr/bin/env python3
"""Compare the complete shared sampling policy with the pinned Rust source."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests" / "cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
REFUSALS = {
    "wrong_precision": (
        "No overload matches call 'the cad direction dot of left with right'",
        "a 32-bit floating-point number",
    ),
    "wrong_dimension": (
        "No overload matches call 'the cad direction dot of left with right'",
        "a fixed array containing 2 items",
    ),
    "wrong_point": ("Variable 'first' requires cad vector3, but this value is a cad vector2",),
    "wrong_tangent": ("Variable 'tangent' requires cad vector3, but this value is a cad vector2",),
}


def verify_refusals(compiler, directory):
    with tempfile.TemporaryDirectory(prefix="refusals-", dir=directory) as temporary:
        for name, diagnostics in REFUSALS.items():
            for level in ("O0", "O2"):
                binary = Path(temporary) / f"{name}-{level}.out"
                status, output, _ = fixtures.run_process(
                    [str(compiler), str(Path(__file__).parent / f"{name}.dl"), f"-{level}", "-o", str(binary)],
                    timeout=30, cwd=ROOT,
                )
                if status != 1 or binary.exists() or any(part not in output for part in diagnostics):
                    raise AssertionError(f"{name} {level}: expected type rejection, got exit {status}\n{output}")
    print("8 precision, dimension and evaluator type rejections passed", flush=True)


def run(command, timeout=30):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"Exit {status}: {command}\n{output}")
    return output, elapsed


def compile_program(command):
    output, elapsed = run(command, 60)
    if output.strip():
        raise AssertionError(f"Unexpected compiler diagnostics:\n{output}")
    print(f"compile {Path(command[-1]).name}: {elapsed:.3f}s", flush=True)


def same(actual, expected, exact=False):
    if math.isnan(expected):
        return math.isnan(actual)
    if actual == expected:
        return actual != 0.0 or math.copysign(1.0, actual) == math.copysign(1.0, expected)
    if exact or not math.isfinite(actual) or not math.isfinite(expected):
        return False
    return math.isclose(actual, expected, rel_tol=3e-13, abs_tol=1e-300)


def cases():
    random_source = random.Random(314159)
    thresholds = [
        -math.inf, -1.0, -0.0, 0.0, 5e-324, 1e-300, 0.01,
        math.nextafter(0.01, 0.0), math.nextafter(0.01, 1.0),
        math.pi / 36.0, math.pi / 24.0, math.pi / 9.0,
        1.0, math.pi / 2.0, 10.0, math.nextafter(10.0, math.inf),
        1e300, math.inf, math.nan,
    ]
    for dimension in (0, 1, 2, 3, 5, 17):
        for index, threshold in enumerate(thresholds):
            left = [1.0] * dimension
            right = [(-1.0 if axis % 2 else 1.0) for axis in range(dimension)]
            yield f"N={dimension} threshold={index}", [0, dimension, threshold, *left, *right]
        for value in (0.0, -0.0, 5e-324, 1e-200, 1e-154, 1e-100, 1e20, 1e100, 1e308, math.inf, math.nan):
            yield f"N={dimension} scale={value}", [0, dimension, 1.0, *([value] * dimension), *([value] * dimension)]
        for index in range(25):
            left = [random_source.uniform(-100, 100) for _ in range(dimension)]
            right = [random_source.uniform(-100, 100) for _ in range(dimension)]
            yield f"N={dimension} random={index}", [0, dimension, random_source.uniform(0.001, 12), *left, *right]
    for kind in (0, 1):
        for turns in (0.25, 1.0, 3.0, -1.0):
            for limit in (0.01, 0.1, math.pi / 2.0, 0.0, math.nan):
                yield f"curve kind={kind} turns={turns} limit={limit}", [1, kind, turns, limit]
    for kind in (2, 3):
        yield f"depth limit kind={kind}", [1, kind, 1.0, 0.1]


def compare(label, case, executables):
    outputs = []
    arguments = [str(value) for value in case]
    for executable in executables:
        output, _ = run([executable, *arguments], 30)
        outputs.append([float(line) for line in output.splitlines()])
    expected, unoptimized, optimized = outputs
    if any(len(actual) != len(expected) for actual in outputs[1:]):
        raise AssertionError(f"{label}: output lengths {[len(v) for v in outputs]}")
    for index, value in enumerate(expected):
        # Counts, direction comparisons, and parameter endpoints are exact.
        exact = case[0] == 1 and index < 5
        for level, actual in (("O0", unoptimized[index]), ("O2", optimized[index])):
            if not same(actual, value, exact):
                raise AssertionError(f"{label} {level} field {index}: {actual!r} != {value!r}")
        if not same(unoptimized[index], optimized[index], True):
            raise AssertionError(f"{label}: O0/O2 differ at field {index}")
    return len(expected) * 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if sys.platform == "win32" else "dynlex"))
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    revision, _ = run([*git, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError("Upstream checkout is not the pinned revision")
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/tessellation.rs"])
    rustc = shutil.which("rustc") or Path.home() / ".cargo/bin/rustc.exe"
    directory = ROOT / "build" / "cadkernel-tessellation-checks"
    directory.mkdir(parents=True, exist_ok=True)
    verify_refusals(arguments.compiler.resolve(), directory)
    driver = directory / "reference-driver.rs"
    driver.write_text(
        f'#[path = r"{(source / "src/tessellation.rs").as_posix()}"]\n'
        'mod tessellation;\n'
        f'include!(r"{(Path(__file__).parent / "reference.rs").as_posix()}");\n',
        encoding="utf-8",
    )
    reference = directory / "reference.out"
    compile_program([rustc, "--edition=2021", "--crate-name", "tessellation_reference", driver, "-O", "-o", reference])
    executables = [reference]
    for level in ("O0", "O2"):
        fixtures.verify_fixture(
            ROOT / "tests/required/cadkernel_tessellation", level,
            arguments.compiler.resolve(), directory, 30, 30, False,
        )
        probe = directory / f"probe-{level}.out"
        compile_program([arguments.compiler.resolve(), Path(__file__).parent / "probe.dl", f"-{level}", "-o", probe])
        executables.append(probe)
    comparisons = 0
    count = 0
    for label, case in cases():
        comparisons += compare(label, case, executables)
        count += 1
        if count % 100 == 0:
            print(f"{count} cases passed", flush=True)
    print(f"{count} cases, {comparisons} Rust comparisons passed; exact O0/O2 numerical parity", flush=True)


if __name__ == "__main__":
    main()
