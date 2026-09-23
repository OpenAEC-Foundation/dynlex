#!/usr/bin/env python3
"""Verify all plane operations against pinned Rust and the original assertions."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
ZERO, X, Y, Z = [0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]


def run(command, timeout=30):
    status, output, _ = fixtures.run_process([str(v) for v in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output


def compile_program(command):
    output = run(command, 60)
    if output.strip():
        raise AssertionError(f"unexpected compiler diagnostics:\n{output}")


def same(actual, expected, exact=False):
    if math.isnan(expected):
        return math.isnan(actual)
    if actual == expected:
        return actual != 0.0 or math.copysign(1.0, actual) == math.copysign(1.0, expected)
    # A tolerance cannot erase a zero, a subnormal, or a nonfinite mismatch.
    if exact or actual == 0.0 or expected == 0.0:
        return False
    return math.isfinite(actual) and math.isfinite(expected) and math.isclose(
        actual, expected, rel_tol=3e-13, abs_tol=0.0)


def parse(output):
    result = {}
    for line in output.splitlines():
        label, token = line.split()
        if label in result:
            raise AssertionError(f"duplicate output field {label}")
        result[label] = math.nan if re.fullmatch(
            r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", token, re.IGNORECASE) else float(token)
    if not result:
        raise AssertionError("empty probe output")
    return result


def frame(mode=0, origin=ZERO, x=X, y=Y, uv=(3.0, -5.0), point=(7.0, -4.0, 9.0),
          xy=(2.0, -3.0), tolerance=1e-9):
    return [mode, *origin, *x, *y, *uv, *point, *xy, tolerance]


def coplanar(points, directions):
    return [2, len(points), len(directions), *(v for p in [*points, *directions] for v in p)]


def cases():
    yield "XY constant", [3]
    yield "default", [4]
    frames = [
        (ZERO, X, Y), (ZERO, Y, X), ([1.0, 2.0, 3.0], [2.0, 0.0, 0.0], [1.0, 3.0, 0.0]),
        ([10.0, 20.0, 30.0], [2.0, 1.0, 5.0], [1.0, 3.0, -2.0]),
        ([100.0, 200.0, 300.0], X, Z), (ZERO, X, X), (ZERO, ZERO, Y), (ZERO, X, ZERO),
        ([512345.678, 4512345.678, 91.5], X, Y), ([-0.0]*3, [1.0, -0.0, -0.0], [-0.0, 1.0, -0.0]),
    ]
    for mode in (0, 1):
        for i, (origin, x, y) in enumerate(frames):
            yield f"frame mode {mode} case {i}", frame(mode, origin, x, y)
        for scale in (5e-324, 1e-200, 1e-162, 2e-162, 1e-100, 1e-12, 1.0, 1e12, 1e77, 1e100, 1e154, 1e155, 1e308):
            yield f"axis magnitude {mode} {scale}", frame(mode, x=[scale, 0.0, 0.0], y=[0.0, scale, 0.0])
    # Adjacent binary64 values exercise inclusive XY and strict angle cutoffs.
    for threshold in (1e-24, 1e-12, 1e-9):
        for value in (math.nextafter(threshold, 0.0), threshold, math.nextafter(threshold, math.inf), 2*threshold):
            yield f"near parallel {value}", frame(x=X, y=[1.0, value, 0.0])
            yield f"orthonormal dot {value}", frame(x=X, y=[value, 1.0, 0.0])
    for tolerance in (-1.0, -0.0, 0.0, math.nextafter(1.0, 0.0), 1.0, math.nextafter(1.0, math.inf), math.inf, math.nan):
        yield f"containment {tolerance}", frame(point=Z, tolerance=tolerance)
    # Every scalar input position, including origin, UV, query XY and tolerance.
    for mode in (0, 1):
        for index in range(1, 18):
            for special in (-0.0, 5e-324, 1e308, -1e308, math.inf, -math.inf, math.nan):
                values = frame(mode, y=Z if mode else Y)
                values[index] = special
                yield f"special mode {mode} field {index} {special}", values
    for points in ([], [ZERO], [ZERO, X], [ZERO, X, Y], [ZERO, X, Y, Z]):
        for directions in ([], [ZERO], [X], [X, Y], [X, Y, Z]):
            yield f"rank points {len(points)} directions {len(directions)}", coplanar(points, directions)
    for scale in (5e-324, 1e-200, 1e-30, 1e-12, 1.0, 1e8, 1e100, 1e308):
        yield f"direction magnitude {scale}", coplanar([ZERO], [X, Y, [0.0, 0.0, scale]])
        yield f"point extent {scale}", coplanar([[scale, 0.0, 0.0], [-scale, 0.0, 0.0]], [])
    for axis in range(3):
        for value in (math.inf, -math.inf, math.nan, -0.0):
            point = ZERO.copy()
            point[axis] = value
            yield f"nonfinite point {axis} {value}", coplanar([point], [])
            yield f"nonfinite empty direction {axis} {value}", coplanar([], [point])
            yield f"nonfinite direction {axis} {value}", coplanar([ZERO], [point])
    floor = 1e-9 + math.ulp(1.0)*64.0
    for height in (math.nextafter(floor, 0.0), floor, math.nextafter(floor, math.inf), 2*floor):
        yield f"point plane tolerance {height}", coplanar([ZERO, X, Y, [0.0, 0.0, height]], [])
    for angle in (math.nextafter(1e-9, 0.0), 1e-9, math.nextafter(1e-9, math.inf), 2e-9):
        for directions in ([X, [1.0, angle, 0.0], Z], [Z, X, [1.0, angle, 0.0]]):
            yield f"ordered direction threshold {angle} {directions[0]}", coplanar([ZERO], directions)
    rng = random.Random(735913)
    for index in range(180):
        origin = [rng.uniform(-1e6, 1e6) for _ in range(3)] if index % 3 == 0 else ZERO
        x, y, point = [[rng.uniform(-10, 10) for _ in range(3)] for _ in range(3)]
        uv, xy = [[rng.uniform(-20, 20) for _ in range(2)] for _ in range(2)]
        for mode in (0, 1):
            yield f"random frame {index} mode {mode}", frame(mode, origin, x, y, uv, point, xy)
        points = [[rng.uniform(-4, 4), rng.uniform(-4, 4), 0.0] for _ in range(index % 12)]
        if points and index % 2:
            points[-1][2] = 1e-7
        directions = [[rng.uniform(-2, 2), rng.uniform(-2, 2), 0.0] for _ in range(index % 4)]
        if directions and index % 5 == 0:
            directions[-1][2] = 1.0
        yield f"random coplanarity {index}", coplanar(points, directions)


def check_mutations(directory, compiler, reference):
    """Each compiled mutant must disagree in the specific field it corrupts."""
    original = (ROOT/"lib/cadkernel/plane.dl").read_text(encoding="utf-8")
    probe = (HERE/"probe.dl").read_text(encoding="utf-8")
    mutations = [
        ("dot-instead-of-gram", "set u to ((dx * yy) - (dy * xy)) / determinant", "set u to dx",
         frame(origin=[1.0, 2.0, 3.0], x=[2.0, 0.0, 0.0], y=[1.0, 3.0, 0.0]), "project.0"),
        ("strict-containment", "(the absolute value of (the value of distance)) <=", "(the absolute value of (the value of distance)) <",
         frame(point=Z, tolerance=1.0), "contains.0"),
        ("unsigned-distance", "true, signedDistance)", "true, the absolute value of signedDistance)",
         frame(point=[0.0, 0.0, -1.0]), "distance.0"),
        ("unscaled-direction-tolerance", "distance <= ((cad plane epsilon) * (the cad length of direction))", "distance <= (cad plane epsilon)",
         coplanar([ZERO], [X, Y, [0.0, 0.0, 1e-30]]), "coplanar.0"),
    ]
    evidence = []
    for name, old, new, values, field in mutations:
        assert original.count(old) == 1, (name, "mutation location changed")
        mutant = directory / f"mutant-{name}.dl"
        mutant.write_text(original.replace(old, new), encoding="utf-8")
        driver = directory / f"mutant-{name}-probe.dl"
        driver.write_text(probe.replace("import lib/cadkernel/plane.dl",
            f"import {mutant.relative_to(ROOT).as_posix()}"), encoding="utf-8")
        binary = directory / f"mutant-{name}.out"
        compile_program([compiler, driver, "-O0", "-o", binary])
        args = [str(v) for v in values]
        expected, actual = parse(run([reference, *args])), parse(run([binary, *args]))
        assert not same(actual[field], expected[field]), (name, "mutation survived", field)
        evidence.append({"mutation": name, "field": field, "expected": expected[field], "actual": actual[field]})
        print(f"mutation detected: {name} ({field})", flush=True)
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT/"build"/("dynlex.exe" if sys.platform == "win32" else "dynlex"))
    parser.add_argument("--rustc", type=Path, default=shutil.which("rustc") or Path.home()/".cargo/bin/rustc.exe")
    args = parser.parse_args()
    source, compiler = args.source.resolve(), args.compiler.resolve()
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    assert run([*git, "rev-parse", "HEAD"]).strip() == PIN
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/space/plane.rs", "src/space/vec.rs"])
    directory = ROOT / "build/cadkernel-plane-checks"
    directory.mkdir(parents=True, exist_ok=True)
    # Remove only this runner's stale success record before doing any checks.
    report = directory / "results.json"
    report.unlink(missing_ok=True)
    observed = [compiler, ROOT/"lib/cadkernel/plane.dl", ROOT/"lib/cadkernel/core.dl",
                ROOT/"lib/std.dl", ROOT/"lib/list.dl", HERE/"probe.dl", HERE/"reference.rs", Path(__file__)]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}
    driver = directory / "reference-driver.rs"
    driver.write_text('#![allow(dead_code)]\nmod space {\n'
        f'#[path=r"{(source/"src/space/vec.rs").as_posix()}"] pub mod vec;\n'
        f'#[path=r"{(source/"src/space/plane.rs").as_posix()}"] pub mod plane;\n'
        '}\n' + f'include!(r"{(HERE/"reference.rs").as_posix()}");\n', encoding="utf-8")
    rustc = args.rustc.resolve()
    upstream_tests = directory / "upstream-tests.out"
    compile_program([rustc, "--edition=2021", "--crate-name", "plane_reference", "--test", driver, "-o", upstream_tests])
    test_output = run([upstream_tests, "space::plane::tests::", "--test-threads=1"])
    assert "13 passed; 0 failed" in test_output, test_output
    (directory/"upstream-tests.txt").write_text(test_output, encoding="utf-8")
    print("13 unchanged upstream Rust tests passed", flush=True)
    reference = directory / "reference.out"
    compile_program([rustc, "--edition=2021", "--crate-name", "plane_reference", driver, "-O", "-o", reference])
    executables = [reference]
    for level in ("O0", "O2"):
        print(level, fixtures.verify_fixture(ROOT/"tests/required/cadkernel_plane", level, compiler, directory, 60, 20, False), flush=True)
        probe = directory / f"probe-{level}.out"
        compile_program([compiler, HERE/"probe.dl", f"-{level}", "-o", probe])
        executables.append(probe)
    count = comparisons = exact_pairs = 0
    data = list(cases())
    (directory/"cases.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    for label, values in data:
        outputs = [parse(run([program, *[str(v) for v in values]])) for program in executables]
        expected, unoptimized, optimized = outputs
        assert list(expected) == list(unoptimized) == list(optimized), (label, "output schema", outputs)
        for field, wanted in expected.items():
            exact = field.startswith(("valid_", "is_", "contains.", "coplanar.", "input."))
            for actual in (unoptimized[field], optimized[field]):
                assert same(actual, wanted, exact), (label, field, wanted, actual, values)
                comparisons += 1
            assert same(unoptimized[field], optimized[field], exact=True), (label, "O0/O2", field, unoptimized[field], optimized[field])
            exact_pairs += 1
        count += 1
        if count % 100 == 0:
            print(f"{count}/{len(data)} cases passed", flush=True)
    mutations = check_mutations(directory, compiler, reference)
    assert hashes == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}, "inputs changed during verification"
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/space/plane.rs", "src/space/vec.rs"])
    report.write_text(json.dumps({"pin": PIN, "rust_tests": 13, "required_modes": ["O0", "O2"],
        "cases": count, "rust_comparisons": comparisons, "exact_optimization_pairs": exact_pairs,
        "relative_tolerance": 3e-13, "absolute_tolerance": 0, "detected_mutations": mutations, "sha256": hashes}, indent=2), encoding="utf-8")
    print(f"PASS: {count} cases; {comparisons} Rust comparisons; {exact_pairs} exact O0/O2 pairs", flush=True)


if __name__ == "__main__":
    main()
