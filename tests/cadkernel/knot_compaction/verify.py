#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify knot compaction against unmodified pinned Rust, with guarded processes."""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
FIXTURE = ROOT / "tests/required/cadkernel_knot_compaction"
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("cadkernel_verifier", ROOT / "tests/cadkernel/verify.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def run(command, timeout=60):
    status, output, elapsed = runner.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT)
    if status:
        raise RuntimeError(f"exit {status}: {command}\n{output}")
    return output, elapsed


def case(name, degree, points, knots, weights, tolerance=1e-12,
         closed=False, strict=True, evaluate=True):
    data = [degree, len(points), len(knots), int(closed), int(strict), tolerance,
            int(evaluate), *(x for p in points for x in p), *knots, *weights]
    return name, data


def hill(offset=0.0):
    return [[0., 0., 0.], [.5, .5, 0.], [1., .5, 0.],
            [1.5 + offset, .5, 0.], [2., 0., 0.]]


def inserted(degree, points, knots, knot):
    """Independent forward insertion creates exactly redundant test controls."""
    span = max(i for i in range(degree, len(points)) if knots[i] <= knot)
    multiplicity = knots.count(knot)
    result = points[:span - degree + 1]
    for index in range(span - degree + 1, span - multiplicity + 1):
        alpha = (knot - knots[index]) / (knots[index + degree] - knots[index])
        result.append([alpha * points[index][axis] + (1.0 - alpha) * points[index - 1][axis]
                       for axis in range(3)])
    result.extend(points[span - multiplicity:])
    return result, knots[:span + 1] + [knot] + knots[span + 1:]


def cases():
    knots = [0., 0., 0., .5, .5, 1., 1., 1.]
    for weight in (.1, 1., 2., 1e-100, 1e100):
        for offset in (0., .125, 1e-8):
            for tolerance in (-0., 1e-12, math.nextafter(.125, 0.), .125, 1.):
                for closed in (False, True):
                    yield case(f"hill-{weight}-{offset}-{tolerance}-{closed}", 2,
                               hill(offset), knots, [weight] * 5, tolerance, closed)
    for tolerance in (-1., -1e-300, math.nan, math.inf, -math.inf):
        yield case(f"invalid-tolerance-{tolerance}", 2, hill(), knots, [1.] * 5, tolerance)
    for weights in ([1., 2., 1., 2., 1.], [1., math.nextafter(1., 2.), 1., 1., 1.],
                    [0.] * 5, [-1.] * 5, [math.inf] * 5, [math.nan] * 5,
                    [0., -0., 0., -0., 0.]):
        yield case(f"permissive-weights-{weights}", 2, hill(), knots, weights,
                   strict=False, evaluate=False)
    yield case("homogeneous-overflow-strict-refusal", 2, [[1e308] * 3] * 5,
               knots, [1e308] * 5, evaluate=False)
    yield case("negative-homogeneous-weights", 2, hill(), knots, [-.1] * 5,
               strict=False, evaluate=False)
    # A failed interior removal must not prevent processing a later knot.
    yield case("overmultiplicity-skipped", 2,
               [[float(i), 0., 0.] for i in range(6)],
               [0., 0., 0., .5, .5, .5, 1., 1., 1.], [1.] * 6)
    # Greville abscissae independently represent an affine line on this domain.
    for degree in (1, 2, 3, 4, 7):
        for multiplicity in sorted({1, degree}):
            k = [2.] * (degree + 1) + [3.] * multiplicity + [5.] * multiplicity + [6.] * (degree + 1)
            p = [[sum(k[i + 1:i + degree + 1]) / degree, 0., 0.]
                 for i in range(len(k) - degree - 1)]
            for weight in (.1, 1., 1e-200):
                yield case(f"greville-{degree}-{multiplicity}-{weight}", degree,
                           p, k, [weight] * len(p), 1e-11)
    rng = random.Random(953546)
    for degree in (2, 3, 4, 6):
        for trial in range(8):
            p = [[rng.uniform(-4., 4.) for _ in range(3)] for _ in range(degree + 1)]
            k = [0.] * (degree + 1) + [1.] * (degree + 1)
            for u in (.25, .75):
                for _ in range(degree):
                    p, k = inserted(degree, p, k, u)
            yield case(f"inserted-{degree}-{trial}", degree, p, k, [.1] * len(p),
                       1e-10, closed=bool(trial % 2))
            if trial == 0:
                changed = [row[:] for row in p]
                changed[2][1] += .125
                yield case(f"first-candidate-fails-later-continues-{degree}", degree,
                           changed, k, [1.] * len(p), 1e-10)
    # No interior knot and minimal control count, including endpoint repetition.
    yield case("minimal-bezier", 2, [[0., 0., 0.], [1., 2., 3.], [4., 5., 6.]],
               [0., 0., 0., 1., 1., 1.], [1.] * 3)


def numbers(output):
    return [float(line) for line in output.splitlines()]


def exact(a, b):
    if math.isnan(a) and math.isnan(b):
        return True
    return a == b and (a != 0 or math.copysign(1., a) == math.copysign(1., b))


def compare(actual, expected, label, data):
    if len(actual) != len(expected):
        raise AssertionError(f"{label}: {len(actual)} fields, expected {len(expected)}")
    geometry = set()
    scale = 1e-300
    if len(expected) > 2 and expected[1] == 1:
        count, knot_count = int(expected[3]), int(expected[4])
        geometry.update(range(8, 8 + count * 3))
        domain_end = 8 + count * 4 + knot_count + 2
        if data[6]:
            geometry.update(range(domain_end, domain_end + 63))
        scale = max((abs(expected[i]) for i in geometry if math.isfinite(expected[i])), default=scale)
    for index, (got, want) in enumerate(zip(actual, expected)):
        if exact(got, want):
            continue
        if index in geometry and math.isclose(got, want, rel_tol=3e-12, abs_tol=3e-12 * scale):
            continue
        raise AssertionError(f"{label}: field {index}: {got!r} != {want!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    args = parser.parse_args()
    source, compiler = args.source.resolve(), args.compiler.resolve()
    rustc = shutil.which("rustc")
    if not rustc:
        raise RuntimeError("rustc is required")
    git = ["git", "-c", f"safe.directory={source.as_posix()}", "-C", source]
    revision, _ = run([*git, "rev-parse", "HEAD"])
    if revision.strip() != PIN:
        raise AssertionError(f"expected source {PIN}, got {revision.strip()}")
    modules = {"tessellation": "src/tessellation.rs", "spline": "src/space/spline.rs",
               "vector": "src/space/vec.rs", "nurbs": "src/space/nurbs.rs",
               "knot_compaction": "src/space/knot_compaction.rs"}
    run([*git, "diff", "--exit-code", "HEAD", "--", *modules.values()])
    if (source / modules["knot_compaction"]).read_text().count("#[test]") != 2:
        raise AssertionError("expected exactly two upstream knot compaction tests")
    directory = Path(tempfile.mkdtemp(prefix="knot-compaction-verify-", dir=ROOT / "build"))
    print(f"Artifacts: {directory}", flush=True)
    def module(name):
        return f'#[path = r"{(source / modules[name]).as_posix()}"] pub mod {name};\n'
    driver = directory / "reference-driver.rs"
    driver.write_text(module("tessellation") + "mod space {\n" + module("spline") +
                      module("vector") + "pub use vector::Vec3;\n" + module("nurbs") +
                      "pub use nurbs::NurbsCurve3;\n" + module("knot_compaction") + "}\n" +
                      f'include!(r"{(HERE / "reference.rs").as_posix()}");\n', encoding="utf-8")
    rust_tests, reference = directory / "upstream.out", directory / "reference.out"
    for extra, output in ((["--test"], rust_tests), ([], reference)):
        diagnostics, _ = run([rustc, "--edition=2021", "--crate-name", "knot_compaction_reference",
                              "-A", "dead_code", driver, *extra, "-O", "-o", output])
        (directory / f"{output.stem}-build.log").write_text(diagnostics, encoding="utf-8")
    upstream, _ = run([rust_tests, "space::knot_compaction::tests::"])
    (directory / "upstream-tests.log").write_text(upstream, encoding="utf-8")
    if "2 passed; 0 failed" not in upstream:
        raise AssertionError(upstream)
    print("Pinned Rust: 2/2 original tests passed", flush=True)
    binaries = {}
    for level in ("O0", "O2"):
        detail = runner.verify_fixture(FIXTURE, level, compiler, directory, 60, 20, False)
        print(f"Fixture {level}: {detail}", flush=True)
        binary = directory / f"probe-{level}.out"
        diagnostics, elapsed = run([compiler, (HERE / "probe.dl").relative_to(ROOT), f"-{level}", "-o", binary])
        (directory / f"probe-{level}-build.log").write_text(diagnostics, encoding="utf-8")
        if diagnostics.strip():
            raise AssertionError(diagnostics)
        print(f"Probe {level}: compiled in {elapsed:.3f}s", flush=True)
        binaries[level] = binary
    comparisons = total = 0
    evidence = []
    for name, data in cases():
        command = [str(value) for value in data]
        expected_text, _ = run([reference, *command], 20)
        expected = numbers(expected_text)
        outputs = {}
        for level, binary in binaries.items():
            text, _ = run([binary, *command], 20)
            outputs[level] = numbers(text)
            compare(outputs[level], expected, f"{name} {level}", data)
            comparisons += len(expected)
        if not all(exact(a, b) for a, b in zip(outputs["O0"], outputs["O2"])):
            raise AssertionError(f"{name}: O0/O2 numeric output differs")
        evidence.append({"case": name, "arguments": command, "rust": expected_text})
        total += 1
        if total % 50 == 0:
            print(f"Differential: {total} cases passed", flush=True)
    (directory / "reference-cases.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(f"PASS: {total} differential cases; {comparisons} Rust field comparisons; exact O0/O2 parity", flush=True)


if __name__ == "__main__":
    main()
