#!/usr/bin/env python3
"""Verify alignment matrices, point application and rejection against pinned Rust."""
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


def arguments(source, target, scale=False, query=(0.25, -2.0, 5.0)):
    return [len(source), len(target), int(scale), *(v for p in [*source, *target] for v in p), *query]


def cases():
    for ns in range(5):
        for nt in range(5):
            yield f"counts {ns} {nt}", arguments([ZERO, X, Y, Z][:ns], [ZERO, Y, Z, X][:nt])
    source = [[1.0, 2.0, 3.0], [3.0, 2.0, 3.0], [1.0, 5.0, 3.0]]
    target = [[7.0, 8.0, 9.0], [7.0, 10.0, 9.0], [4.0, 8.0, 9.0]]
    for scale in (False, True):
        yield f"original spatial frame scale {scale}", arguments(source, target, scale)
        yield f"unequal spacing scale {scale}", arguments(source, [target[0], [7.0, 12.0, 9.0], [-5.0, 8.0, 9.0]], scale)
        for a in (X, Y, Z, [0.6, 0.0, 0.8], [0.0, 0.6, 0.8]):
            for b in (a, [-v for v in a], X, Y, Z):
                yield f"rotation {a} {b} scale {scale}", arguments([ZERO, a], [ZERO, b], scale)
    for vertical in (math.nextafter(0.9, 0.0), 0.9, math.nextafter(0.9, 1.0), -0.9):
        axis = [math.sqrt(1 - vertical*vertical), 0.0, vertical]
        yield f"half turn preferred axis {vertical}", arguments([ZERO, axis], [ZERO, [-v for v in axis]])
    for value in (0.0, 5e-324, 1e-200, math.nextafter(1e-12, 0.0), 1e-12,
                  math.nextafter(1e-12, math.inf), 2e-12, 1e100, 1e154, 1e155, 1e308):
        for scale in (False, True):
            for swap in (False, True):
                a, b = [ZERO, [value, 0.0, 0.0]], [ZERO, Y]
                yield f"baseline {value} scale {scale} swap {swap}", arguments(b if swap else a, a if swap else b, scale)
        for swap in (False, True):
            a, b = [ZERO, X, [1.0, value, 0.0]], [ZERO, X, Y]
            yield f"three normal cutoff {value} swap {swap}", arguments(b if swap else a, a if swap else b)
        for cosine_sign in (-1.0, 1.0):
            yield f"sine cutoff {value} cosine {cosine_sign}", arguments([ZERO, X], [ZERO, [cosine_sign, value, 0.0]])
    for ns in (1, 2, 3):
        for side in ("source", "target"):
            for point in range(ns):
                for axis in range(3):
                    for special in (math.nan, math.inf, -math.inf, 1e308, -0.0):
                        a = [p.copy() for p in source[:ns]]
                        b = [p.copy() for p in target[:ns]]
                        (a if side == "source" else b)[point][axis] = special
                        yield f"special n{ns} {side} point{point} axis{axis} {special}", arguments(a, b, True)
    for ns in (1, 2, 3):
        for axis in range(3):
            for special in (math.nan, math.inf, -math.inf, -0.0, 5e-324, 1e308):
                query = [0.25, -2.0, 5.0]
                query[axis] = special
                yield f"query n{ns} axis{axis} {special}", arguments(source[:ns], target[:ns], query=query)
    yield "overflow one pair translation", arguments([[-1e308, 0.0, 0.0]], [[1e308, 0.0, 0.0]])
    yield "overflow baseline subtraction", arguments([[-1e308, 0.0, 0.0], [1e308, 0.0, 0.0]], [ZERO, X])
    yield "overflow scaled translation", arguments([[1e150, 0.0, 0.0], [1e150, 1e-11, 0.0]], [ZERO, [0.0, 1e150, 0.0]], True)
    for ns in (1, 2, 3):
        for sign in (-0.0, 0.0):
            a = [[sign, sign, sign], [1.0, sign, sign], [sign, 1.0, sign]][:ns]
            yield f"signed zero {ns} {sign}", arguments(a, a, True, [sign]*3)
    # Third points may normalize to zero after overflow, or coincide with the origin.
    for third in (ZERO, [1e-200, 0.0, 0.0], [1e308, 0.0, 0.0], [0.0, 1e308, 0.0]):
        yield f"third direction source {third}", arguments([ZERO, X, third], [ZERO, X, Y])
        yield f"third direction target {third}", arguments([ZERO, X, Y], [ZERO, X, third])
    rng = random.Random(913735)
    for index in range(180):
        ns = 1 + index % 3
        source = [[rng.uniform(-100, 100) for _ in range(3)] for _ in range(ns)]
        target = [[rng.uniform(-100, 100) for _ in range(3)] for _ in range(ns)]
        if index % 4 == 0:
            source = [[v + offset for v, offset in zip(p, [512345.678, 4512345.678, 91.5])] for p in source]
        for scale in (False, True):
            yield f"random {index} scale {scale}", arguments(source, target, scale)


def check_mutations(directory, compiler, reference):
    """Reject compiled algorithm changes, never count a compile error as a catch."""
    original = (ROOT/"lib/cadkernel/alignment.dl").read_text(encoding="utf-8")
    probe = (HERE/"probe.dl").read_text(encoding="utf-8")
    mutations = [
        ("reversed-translation", "the cad difference of targetOrigin and (columnX scaled", "the cad sum of targetOrigin and (columnX scaled",
         arguments([[1.0, 2.0, 3.0]], [[7.0, 8.0, 9.0]]), "matrix.3"),
        ("wrong-half-turn-axis", "set preferred to basisZ", "set preferred to basisY",
         arguments([ZERO, X], [ZERO, [-1.0, 0.0, 0.0]]), "matrix.10"),
        ("lost-two-pair-scale", "set scale to bLength / aLength", "set scale to 1.0 as a 64 bit floating-point number",
         arguments([ZERO, X], [ZERO, [0.0, 2.0, 0.0]], True), "matrix.4"),
        ("strict-baseline-cutoff", "(aLength <= 1e-12)", "(aLength < 1e-12)",
         arguments([ZERO, [1e-12, 0.0, 0.0]], [ZERO, X]), "valid.0"),
    ]
    evidence = []
    for name, old, new, values, field in mutations:
        assert original.count(old) == 1, (name, "mutation location changed")
        mutant = directory / f"mutant-{name}.dl"
        mutant.write_text(original.replace(old, new), encoding="utf-8")
        driver = directory / f"mutant-{name}-probe.dl"
        driver.write_text(probe.replace("import lib/cadkernel/alignment.dl",
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
    source, compiler, rustc = args.source.resolve(), args.compiler.resolve(), args.rustc.resolve()
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    assert run([*git, "rev-parse", "HEAD"]).strip() == PIN
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/space/alignment.rs", "src/space/vec.rs"])
    directory = ROOT / "build/cadkernel-alignment-checks"
    directory.mkdir(parents=True, exist_ok=True)
    report = directory / "results.json"
    report.unlink(missing_ok=True)
    observed = [compiler, ROOT/"lib/cadkernel/alignment.dl", ROOT/"lib/cadkernel/core.dl",
                ROOT/"lib/std.dl", ROOT/"lib/list.dl", HERE/"probe.dl", HERE/"reference.rs", Path(__file__)]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in observed}
    driver = directory / "reference-driver.rs"
    driver.write_text('#![allow(dead_code)]\nmod space {\n'
        f'#[path=r"{(source/"src/space/vec.rs").as_posix()}"] pub mod vec;\n'
        'pub use vec::Vec3;\n'
        f'#[path=r"{(source/"src/space/alignment.rs").as_posix()}"] pub mod alignment;\n'
        '}\n' + f'include!(r"{(HERE/"reference.rs").as_posix()}");\n', encoding="utf-8")
    upstream_tests = directory / "upstream-tests.out"
    compile_program([rustc, "--edition=2021", "--crate-name", "alignment_reference", "--test", driver, "-o", upstream_tests])
    test_output = run([upstream_tests, "space::alignment::tests::", "--test-threads=1"])
    assert "1 passed; 0 failed" in test_output, test_output
    (directory/"upstream-tests.txt").write_text(test_output, encoding="utf-8")
    print("1 unchanged upstream Rust test passed", flush=True)
    reference = directory / "reference.out"
    compile_program([rustc, "--edition=2021", "--crate-name", "alignment_reference", driver, "-O", "-o", reference])
    executables = [reference]
    for level in ("O0", "O2"):
        print(level, fixtures.verify_fixture(ROOT/"tests/required/cadkernel_alignment", level, compiler, directory, 60, 20, False), flush=True)
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
            exact = field.startswith(("valid.", "input.")) or field in ("matrix.12", "matrix.13", "matrix.14", "matrix.15")
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
    run([*git, "diff", "--exit-code", "HEAD", "--", "src/space/alignment.rs", "src/space/vec.rs"])
    report.write_text(json.dumps({"pin": PIN, "rust_tests": 1, "required_modes": ["O0", "O2"],
        "cases": count, "rust_comparisons": comparisons, "exact_optimization_pairs": exact_pairs,
        "relative_tolerance": 3e-13, "absolute_tolerance": 0, "detected_mutations": mutations, "sha256": hashes}, indent=2), encoding="utf-8")
    print(f"PASS: {count} cases; {comparisons} Rust comparisons; {exact_pairs} exact O0/O2 pairs", flush=True)


if __name__ == "__main__":
    main()
