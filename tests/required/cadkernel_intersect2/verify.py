# SPDX-License-Identifier: MPL-2.0
# Numerical verification against cadkernel 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
# Source: https://github.com/HakanSeven12/cadkernel/tree/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/geom2d
"""Run translated fixtures, invalid tolerances and runtime Rust comparisons.

Usage: python verify.py --upstream /path/to/the/pinned/cadkernel/checkout
Only compiler artifacts in a temporary subdirectory beside this script are written.
"""

import argparse
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time


REVISION = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes
INVALID = HERE.parent / "cadkernel_tolerance_invalid"


def run(command, *, expected_status=0, source=None):
    with unattended_child_processes():
        result = subprocess.run(
            [str(part) for part in command], cwd=ROOT, input=source,
            text=True, capture_output=True, timeout=45,
        )
    if result.returncode != expected_status:
        raise AssertionError(
            f"{command}: exit {result.returncode}, expected {expected_status}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def upstream_module(upstream):
    """Use the exact shared definitions and unit tests; avoid unrelated modules."""
    source = (upstream / "src/geom2d/mod.rs").read_text(encoding="utf-8")
    start = source.index("#[derive(Debug, Clone, Copy, PartialEq)]\npub struct Ellipse")
    tests_start = source.index("#[cfg(test)]", start)
    opening = source.index("{", tests_start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    intersect = (upstream / "src/geom2d/intersect.rs").as_posix()
    return (
        "mod geom2d {\n" + source[start:tests_start] + source[tests_start:end]
        + f'\n#[path = "{intersect}"] pub mod intersect;\n}}\n'
    )


RUST_DRIVER = r'''
use geom2d::{Ellipse, Tolerance, intersect::*};
fn scalar(value: f64) { println!("{:.17e}", value); }
fn scalars(values: Vec<f64>) {
    println!("{}", values.len());
    for value in values { scalar(value); }
}
fn main() {
    let args: Vec<String> = std::env::args().collect();
    let values: Vec<f64> = args[2..].iter().map(|s| s.parse().unwrap()).collect();
    let v = |i: usize| [values[i], values[i + 1]];
    let ellipse = |i: usize| Ellipse {
        centre: v(i), major_radius: values[i + 2], minor_radius: values[i + 3],
        major_axis: v(i + 4),
    };
    match args[1].as_str() {
        "line-line" => match line_line(v(0), v(2), v(4), v(6)) {
            None => println!("0"),
            Some((a, b)) => { println!("1"); scalar(a); scalar(b); }
        },
        "line-circle" => scalars(line_circle(v(0), v(2), v(4), values[6])),
        "circle-angles" => scalars(circle_circle_angles(v(0), values[2], v(3), values[5])),
        "circle-points" => {
            let hits = circle_circle_points(v(0), values[2], v(3), values[5]);
            println!("{}", hits.len());
            for p in hits { scalar(p[0]); scalar(p[1]); }
        },
        "line-ellipse" => {
            let hits = line_ellipse(v(0), v(2), &ellipse(4));
            println!("{}", hits.len());
            for (a, b) in hits { scalar(a); scalar(b); }
        },
        "ellipse-point" => {
            let point = ellipse(0).point_at(values[6]);
            println!("1"); scalar(point[0]); scalar(point[1]);
        },
        "ellipse-minor" => {
            let point = ellipse(0).minor_axis();
            println!("1"); scalar(point[0]); scalar(point[1]);
        },
        "ellipse-degenerate" => println!("{}", ellipse(0).is_degenerate()),
        "tolerance-equal" => println!("{}", Tolerance::new(values[0]).are_equal(values[1], values[2])),
        _ => panic!("unknown operation"),
    }
}
'''


def fixtures():
    rng = random.Random(953546)
    cases = []

    def add(operation, *values):
        cases.append((operation, [float(value) for value in values]))

    def circles(*values):
        add("circle-angles", *values)
        add("circle-points", *values)

    for determinant in [0.0, -0.0, 5e-11, 1e-10, -1e-10, 2e-10]:
        add("line-line", 0, 0, 1, 0, 0, 1, 1, determinant)
    for radius in [0.0, -5.0, 1e-8, 1e-7, 5.0, math.nan, math.inf]:
        add("line-circle", 0, 0, 1, 0, 0, 0, radius)
    for direction in [0.0, 5e-11, 1e-10, 1.0, -1.0, math.nan, math.inf]:
        add("line-circle", -10, 5, direction, 0, 0, 0, 5)
    for distance in [0.0, 5e-10, 1e-9, 1.0, 2.0, 2.0 + 5e-10, 3.0]:
        circles(0, 0, 1, distance, 0, 1)
    circles(0, 0, 5, 0.5, 0, 1)
    circles(0, 0, 2, 1, 0, 1)
    circles(0, 0, 1e-9, 1.9e-9, 0, 1e-9)
    circles(0, 0, math.nan, 1, 0, 1)
    circles(0, 0, math.inf, 1, 0, 1)
    circles(0, 0, -1, 1, 0, 1)
    for radius in [0.0, 5e-21, 1e-20, -3.0, 3.0, math.nan, math.inf]:
        for operation in ["line-ellipse", "ellipse-degenerate"]:
            shape = [0, 0, radius, 2, 1, 0]
            add(operation, *([0, 0, 1, 0] + shape if operation == "line-ellipse" else shape))
    for y in [0.0, 1.0, 2.0, 2.0 + 1e-15, 3.0]:
        add("line-ellipse", -10, y, 1, 0, 0, 0, 3, 2, 1, 0)
    for parameter in [-0.0, -math.pi, math.pi / 2, 10 * math.tau + 0.3, math.nan, math.inf]:
        add("ellipse-point", 4, -1, 5, 2, 0.6, 0.8, parameter)
    for left, right in [(0, 1e-3), (1e-3, 0), (0, 1.001e-3), (math.nan, 0), (math.inf, math.inf)]:
        add("tolerance-equal", 1e-3, left, right)

    for index in range(80):
        origin = [rng.uniform(-10, 10), rng.uniform(-10, 10)]
        direction = [rng.uniform(-3, 3), rng.uniform(-3, 3)]
        other = [rng.uniform(-10, 10), rng.uniform(-10, 10)]
        other_direction = [rng.uniform(-3, 3), rng.uniform(-3, 3)]
        add("line-line", *origin, *direction, *other, *other_direction)
        radius = rng.uniform(0.01, 10)
        add("line-circle", *origin, *direction, *other, radius)
        circles(*origin, radius, *other, rng.uniform(0.01, 10))
        rotation = rng.uniform(-math.pi, math.pi)
        axis = [math.cos(rotation), math.sin(rotation)]
        shape = [*other, radius, rng.uniform(0.01, 5), *axis]
        if index % 5 == 0:
            shape[2] = -shape[2]
        if index % 7 == 0:
            shape[4] *= 2
            shape[5] *= 2
        add("line-ellipse", *origin, *direction, *shape)
        add("ellipse-point", *shape, rng.uniform(-20, 20))
        add("ellipse-minor", *shape)
        add("ellipse-degenerate", *shape)
        linear = 10 ** rng.uniform(-12, 3)
        first = rng.uniform(-5, 5)
        add("tolerance-equal", linear, first, first + rng.uniform(-2, 2) * linear)

    for offset in [512345.678, 4512345.678, 1e10]:
        add("line-line", offset, offset, 1, 0, offset + 5, offset - 5, 0, 1)
        add("line-circle", offset - 10, offset, 1, 0, offset, offset, 5)
        circles(offset, offset, 2, offset, offset + 3, 2)
        add("line-ellipse", offset - 10, offset, 1, 0, offset, offset, 3, 2, 1, 0)
    return cases


def scalar(text):
    # C printf may retain a NaN payload spelling, including Windows nan(ind).
    if re.fullmatch(r"[+-]?nan(?:\([^)]*\))?", text, re.IGNORECASE):
        return math.nan
    return float(text)


def compare(actual, expected, context):
    got, want = actual.split(), expected.split()
    if not got or len(got) != len(want) or got[0] != want[0]:
        raise AssertionError(f"{context}: result shape differs: {got} != {want}")
    for index, (left, right) in enumerate(zip(got[1:], want[1:])):
        a, b = scalar(left), scalar(right)
        if math.isnan(a) and math.isnan(b):
            continue
        if a == 0.0 and b == 0.0 and math.copysign(1, a) != math.copysign(1, b):
            raise AssertionError(f"{context}: signed zero differs at {index}")
        if not math.isclose(a, b, rel_tol=2e-12, abs_tol=2e-12):
            raise AssertionError(f"{context}: scalar {index}: {a!r} != {b!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc")
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    revision = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "rev-parse", "HEAD"]).strip()
    if revision != REVISION:
        raise AssertionError(f"Expected pinned source {REVISION}, got {revision}")
    print(f"Upstream: {revision}", flush=True)
    print(f"Compiler: {args.compiler}; version {run([args.compiler, '--version']).strip()}", flush=True)
    source = upstream_module(upstream)
    cases = fixtures()
    with tempfile.TemporaryDirectory(prefix=".verify-", dir=HERE) as scratch:
        directory = Path(scratch).resolve()
        assert directory.parent == HERE
        rust_tests = directory / "upstream-tests.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_intersect_tests", "--edition=2021", "--test", "-A", "warnings", "-C", "opt-level=2", "-o", rust_tests], source=source)
        print(run([rust_tests, "--quiet"]).strip(), flush=True)
        reference = directory / "reference.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_intersect_reference", "--edition=2021", "-A", "warnings", "-C", "opt-level=2", "-o", reference], source=source + RUST_DRIVER)
        references = [run([reference, operation, *map(repr, values)]) for operation, values in cases]
        for level in ["-O0", "-O2"]:
            for fixture in [HERE, INVALID]:
                binary = directory / f"{fixture.name}{level}.out"
                started = time.perf_counter()
                run([args.compiler, fixture / "main.dl", level, "-o", binary])
                elapsed = time.perf_counter() - started
                expected = (fixture / "expected.txt").read_text(encoding="utf-8")
                if fixture == INVALID:
                    for selection in [None, "negative-zero", "negative", "infinity", "negative-infinity", "nan"]:
                        command = [binary] if selection is None else [binary, selection]
                        assert run(command, expected_status=1) == expected, selection
                    print(f"{level}: all 6 invalid tolerances rejected; compile {elapsed:.3f}s", flush=True)
                else:
                    assert run([binary]) == expected
                    print(f"{level}: all translated and boundary fixtures passed; compile {elapsed:.3f}s", flush=True)
            driver = directory / f"differential{level}.out"
            run([args.compiler, HERE / "differential.dl", level, "-o", driver])
            for (operation, values), expected in zip(cases, references):
                actual = run([driver, operation, *map(repr, values)])
                compare(actual, expected, (level, operation, values))
            print(f"{level}: {len(cases)} runtime-input differential cases passed", flush=True)
    print("All checks passed; -O0 and -O2 agree with the pinned Rust reference.", flush=True)


if __name__ == "__main__":
    main()
