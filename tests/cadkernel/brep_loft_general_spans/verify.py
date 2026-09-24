#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare exact pinned Rust rational_spans with native O0/O2 output."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import random
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SOURCE_HASH = "e0e38b79eb944f6b4e5a2fa263c7deb6b12e4ff2d2da238d327720fbdaf4fd98"


def run(command: list[str | Path], *, timeout: float = 120) -> str:
    code, output, _ = fixtures.run_process(
        [str(item) for item in command], timeout=timeout, cwd=ROOT,
    )
    if code:
        raise AssertionError(f"exit {code}: {command}\n{output}")
    return output.strip()


def source_function(text: str, signature: str) -> str:
    start = text.index(signature)
    first_brace = text.index("{", start)
    depth = 0
    for index in range(first_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unclosed source function {signature}")


def case_args(degree: int, points: list[tuple[float, float, float]],
              knots: list[float], weights: list[float]) -> list[str]:
    values = [degree, len(points), *(axis for point in points for axis in point),
              len(knots), *knots, len(weights), *weights]
    return [format(value, ".17g") for value in values]


def cases() -> list[tuple[str, list[str], int]]:
    line = [(0., 0., 0.), (2., -1., 3.)]
    quadratic = [(0., 0., 0.), (1., 2., 3.), (3., 0., -1.)]
    cubic = [(0., 0., 0.), (1., 2., 3.), (2., -1., 4.), (4., 0., 0.)]
    result = [
        ("linear-clamped", 1, line, [0., 0., 1., 1.], [1., 1.], 0),
        ("linear-affine-domain", 1, line, [-3., -3., 7., 7.], [2., 3.], 0),
        ("weighted-quadratic", 2, quadratic, [0., 0., 0., 1., 1., 1.], [1., .5, 2.], 0),
        ("weighted-cubic", 3, cubic, [0.]*4 + [1.]*4, [2., 4., .5, 3.], 0),
        ("two-quadratic-spans", 2, quadratic + [(5., 1., 2.)], [0.]*3 + [.4] + [1.]*3, [1., 2., 3., 4.], 0),
        ("two-cubic-spans", 3, cubic + [(5., 2., 2.)], [0.]*4 + [.25] + [1.]*4, [1., 2., .75, 4., 1.], 0),
        ("already-split-quadratic", 2, quadratic + [(4., 1., 2.), (5., 2., 3.)], [0.]*3 + [.5, .5] + [1.]*3, [1.]*5, 0),
        ("clustered-interior-knots", 2, quadratic + [(4., 1., 2.), (5., 2., 3.)],
         [0.]*3 + [.5, .5 + 5e-11] + [1.]*3, [1.]*5, 0),
        ("near-end-interior-knot", 2, quadratic + [(4., 1., 2.)],
         [0.]*3 + [1. - 5e-13] + [1.]*3, [1.]*4, 0),
        ("unclamped-quadratic", 2, quadratic + [(5., 1., 2.)], [float(i) for i in range(7)], [1., 2., 3., 4.], 0),
        ("periodic-like-cubic", 3, cubic + [(5., 2., 2.), (6., 1., 3.)], [float(i) for i in range(10)], [1., 2., 3., 4., 2., 1.], 0),
        ("translated-weighted", 2, [(1e6+x, -1e6+y, 7.+z) for x,y,z in quadratic], [2.]*3 + [6.]*3, [1., .25, 3.], 0),
        ("extra-weight-ignored", 1, line, [0., 0., 1., 1.], [1., 1., -7.], 0),
        ("valid-max-degree", 24, [(float(i), float(i % 3), 0.) for i in range(25)],
         [0.]*25 + [1.]*25, [1.]*25, 0),
        ("invalid-negative-degree", -1, line, [0., 0., 1., 1.], [1., 1.], 1),
        ("invalid-zero-degree", 0, line, [0., 0., 1.], [1., 1.], 1),
        ("invalid-high-degree", 25, line, [0., 0., 1., 1.], [1., 1.], 1),
        ("invalid-too-few-controls", 2, line, [0.]*5, [1., 1.], 1),
        ("invalid-knot-count", 1, line, [0., 0., 1.], [1., 1.], 1),
        ("invalid-short-weights", 1, line, [0., 0., 1., 1.], [1.], 2),
        ("invalid-zero-weight", 1, line, [0., 0., 1., 1.], [0., 1.], 2),
        ("invalid-negative-weight", 1, line, [0., 0., 1., 1.], [-1., 1.], 2),
        ("invalid-infinite-weight", 1, line, [0., 0., 1., 1.], [math.inf, 1.], 2),
        ("invalid-nan-weight", 1, line, [0., 0., 1., 1.], [math.nan, 1.], 2),
        ("invalid-homogeneous-overflow", 1, [(1e308, 0., 0.), line[1]], [0., 0., 1., 1.], [10., 1.], 2),
        ("invalid-active-domain", 1, line, [0., 0., 0., 0.], [1., 1.], 3),
        ("invalid-inverted-knots", 1, line, [0., 2., 1., 3.], [1., 1.], 3),
        ("invalid-nan-start", 1, line, [0., math.nan, 1., 1.], [1., 1.], 3),
        ("nan-interior-source-behavior", 2, quadratic + [(4., 1., 2.)],
         [0., 0., 0., math.nan, 1., 1., 1.], [1.]*4, 0),
        ("no-nonzero-span", 1, line + [(3., 2., 4.), (4., 3., 5.)],
         [0., 0., .6e-12, 1.2e-12, 1.8e-12, 1.8e-12], [1.]*4, 6),
    ]
    rng = random.Random(953546)
    for index in range(36):
        degree = rng.randint(1, 5)
        interior = sorted(rng.uniform(.05, .95) for _ in range(rng.randrange(4)))
        control_count = degree + len(interior) + 1
        points = [tuple(rng.uniform(-8., 8.) for _ in range(3)) for _ in range(control_count)]
        weights = [rng.uniform(.2, 4.) for _ in range(control_count)]
        result.append((f"random-{index}", degree, points,
                       [0.]*(degree+1) + interior + [1.]*(degree+1), weights, 0))
    return [(name, case_args(degree, points, knots, weights), error)
            for name, degree, points, knots, weights, error in result]


def compare(name: str, rust: str, native: str, expected_error: int) -> int:
    left = [float(line) for line in rust.splitlines()]
    right = [float(line) for line in native.splitlines()]
    if len(left) != len(right):
        raise AssertionError(f"{name}: field count Rust={len(left)} native={len(right)}\n{rust}\n{native}")
    if left[:3] != right[:3] or int(left[1]) != expected_error:
        raise AssertionError(f"{name}: status Rust={left[:3]} native={right[:3]} expected error={expected_error}")
    cursor = 3
    for _ in range(int(left[2])):
        if left[cursor + 2] != right[cursor + 2]:
            raise AssertionError(f"{name}: control count mismatch at field {cursor+2}")
        cursor += 3 + 4 * int(left[cursor + 2])
    if cursor != len(left):
        raise AssertionError(f"{name}: malformed span serialization")
    for index, (a, b) in enumerate(zip(left[3:], right[3:]), start=3):
        if not (math.isclose(a, b, rel_tol=3e-13, abs_tol=1e-11) or
                (math.isnan(a) and math.isnan(b))):
            raise AssertionError(f"{name}: field {index}: Rust={a}, DynLex={b}")
    return len(left)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--case", default="")
    args = parser.parse_args()
    source = args.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source.as_posix()}",
                    "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_text = (source / "src/brep/loft_general.rs").read_text(encoding="utf-8")
    digest = hashlib.sha256((source / "src/brep/loft_general.rs").read_bytes()).hexdigest()
    if digest != SOURCE_HASH:
        raise AssertionError(f"source hash {digest} != {SOURCE_HASH}")
    functions = "\n\n".join(source_function(source_text, signature) for signature in (
        "fn mix(", "fn homogeneous(", "fn unique(", "fn rational_spans(",
    ))
    suffix = ".exe" if sys.platform == "win32" else ".out"
    tested = fields = 0
    with tempfile.TemporaryDirectory(prefix="cad-loft-general-spans-", dir=ROOT / "build") as directory:
        temporary = Path(directory)
        driver = temporary / "driver.rs"
        driver.write_text((HERE / "reference.rs").read_text(encoding="utf-8").replace(
            "// PINNED_SOURCE_FUNCTIONS", functions), encoding="utf-8")
        for mode in ("O0", "O2"):
            rust_binary = temporary / f"reference-{mode}{suffix}"
            native_binary = temporary / f"native-{mode}{suffix}"
            run([args.rustc, "--edition=2021", driver, "-C", f"opt-level={mode[1]}",
                 "-o", rust_binary], timeout=180)
            run([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary], timeout=180)
            for name, parameters, error in cases():
                if args.case and args.case not in name:
                    continue
                rust_output = run([rust_binary, *parameters], timeout=15)
                native_output = run([native_binary, *parameters], timeout=15)
                fields += compare(f"{name}/{mode}", rust_output, native_output, error)
                tested += 1
            fixture = ROOT / "tests/required/cadkernel_brep_loft_general_spans"
            if fixture.is_dir() and not args.case:
                print(f"fixture {mode}: {fixtures.verify_fixture(fixture, mode, args.compiler.resolve(), temporary, 180, 60, False)}", flush=True)
    if not tested:
        raise AssertionError("case filter selected no cases")
    print(f"PASS: {tested // 2} cases at O0/O2, {fields} compared fields; "
          f"source={revision[:7]} sha256={digest[:12]}", flush=True)


if __name__ == "__main__":
    main()
