# SPDX-License-Identifier: MPL-2.0
# Source: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/geom2d/gradient.rs
"""Verify the complete gradient module against pinned Rust at O0 and O2.

Usage: python verify.py --upstream /path/to/pinned/cadkernel
The upstream module has zero unit tests. This runs independent fixtures,
negative type checks and runtime comparisons with its unmodified function.
Windows defaults to the MSVC Rust target to match the native C math runtime;
the GNU target's math implementation differs for near-quadrant and huge angles.
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


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
REVISION = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "scripts"))
from process_error_mode import unattended_child_processes


def invoke(command, *, source=None):
    with unattended_child_processes():
        return subprocess.run(
            list(map(str, command)), cwd=ROOT, input=source,
            capture_output=True, text=True, encoding="utf-8", timeout=45,
        )


def run(command, *, source=None):
    result = invoke(command, source=source)
    if result.returncode:
        raise AssertionError(f"{command}: exit {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


def pinned_source(upstream, name):
    source = (upstream / name).read_text(encoding="utf-8")
    pinned = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "show", f"{REVISION}:{name}"])
    if source != pinned:
        raise AssertionError(f"{name} differs from the pinned Git blob")
    return source


def reference_source(upstream):
    pinned_source(upstream, "src/geom2d/gradient.rs")
    common = pinned_source(upstream, "src/geom2d/mod.rs")
    start = common.index("#[derive(Debug, Clone, Copy, PartialEq)]\npub struct Tolerance")
    end = common.index("#[cfg(test)]", start)
    gradient = (upstream / "src/geom2d/gradient.rs").as_posix()
    return "mod geom2d {\n" + common[start:end] + f'#[path = "{gradient}"] pub mod gradient;\n}}\n'


RUST_DRIVER = r'''
fn main() {
    let values: Vec<f64> = std::env::args().skip(1)
        .map(|value| value.parse().unwrap()).collect();
    let boundary: Vec<[f64; 2]> = values[3..].chunks_exact(2)
        .map(|pair| [pair[0], pair[1]]).collect();
    let tolerance = geom2d::Tolerance::new(values[2]);
    match geom2d::gradient::gradient_frame(&boundary, values[0], values[1], tolerance) {
        None => println!("false"),
        Some(frame) => {
            println!("true");
            for value in [frame.center[0], frame.center[1], frame.projection_min, frame.projection_span, frame.radius] {
                println!("{:.17e}", value);
            }
        }
    }
}
'''


MATH_DRIVER = r'''
unsafe extern "C" { fn cos(value: f64) -> f64; }
fn main() {
    let value: f64 = std::env::args().nth(1).unwrap().parse().unwrap();
    println!("{:.17e}\n{:.17e}\n{:.17e}", value, value.cos(), unsafe { cos(value) });
}
'''


def fixtures():
    maximum = sys.float_info.max
    box = [(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (0.0, 2.0)]
    boundaries = [
        [], [(0.0, 0.0)], [(3.0, 4.0)], box,
        [(0.0, 0.0), (0.0, 0.0)],
        [(math.nan, 1.0), (2.0, math.inf)],
        [*box, (math.nan, 100.0), (100.0, -math.inf)],
        [(-0.0, 0.0), (0.0, -0.0)],
        [(1e308, 0.0), (1.5e308, 0.0)],
        [(-1e308, 0.0), (1e308, 0.0)],
        [(-maximum, -maximum)], [(maximum, maximum)],
        [(0.0, 0.0), (maximum, maximum)],
        [(-maximum, -maximum), (0.0, 0.0)],
        [(-1e-300, -1e-300), (1e-300, 1e-300)],
        [(0.0, 0.0), (1e200, 1e200)],
        [(512345.678, 4512345.678), (512345.679, 4512345.68)],
    ]
    cases = []

    def add(points, angle, shift, linear=1e-7):
        cases.append([float(angle), float(shift), float(linear), *(float(value) for point in points for value in point)])

    for points in boundaries:
        for angle in (0.0, math.pi / 4, -math.pi / 2):
            for shift in (-1.0, 0.0, 0.5, 1.0, 2.0):
                add(points, angle, shift)
                if len(points) > 1:
                    add(list(reversed(points)), angle, shift)
    for angle in (math.nan, math.inf, -math.inf, 1e308, -1e308):
        for shift in (0.0, 0.5, math.nan, math.inf, -math.inf):
            add(box, angle, shift)
    for linear in (math.ulp(0.0), 1e-12, 10.0, 1e308, maximum):
        add(box, 0.0, 0.0, linear)
        add([(3.0, 4.0)], 2.3, 0.3, linear)
    rng = random.Random(953546)
    for _ in range(150):
        scale = rng.choice((1e-150, 1e-3, 1.0, 1e9, 1e150))
        points = [(rng.uniform(-10, 10) * scale, rng.uniform(-10, 10) * scale) for _ in range(rng.randrange(1, 9))]
        angle, shift = rng.uniform(-1000 * math.tau, 1000 * math.tau), rng.uniform(-2, 3)
        linear = 10 ** rng.uniform(-12, 3)
        add(points, angle, shift, linear)
        add(list(reversed(points)), angle, shift, linear)
    return cases


def scalar(value):
    if re.fullmatch(r"[+-]?nan(?:\([^)]*\))?", value, re.IGNORECASE):
        return math.nan
    return float(value)


def compare(actual, expected, context):
    got, want = actual.split(), expected.split()
    if not got or len(got) != len(want) or got[0] != want[0]:
        raise AssertionError(f"{context}: result shape differs: {got} != {want}")
    for index, (left, right) in enumerate(zip(got[1:], want[1:])):
        a, b = scalar(left), scalar(right)
        if math.isnan(a) and math.isnan(b):
            continue
        if a == 0.0 and b == 0.0 and math.copysign(1, a) != math.copysign(1, b):
            raise AssertionError(f"{context}: result {index}: signed zero differs")
        if not math.isclose(a, b, rel_tol=2e-12, abs_tol=0.0):
            raise AssertionError(f"{context}: result {index}: {a!r} != {b!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc")
    parser.add_argument("--rust-target", default="x86_64-pc-windows-msvc" if os.name == "nt" else None)
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    revision = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "rev-parse", "HEAD"]).strip()
    if revision != REVISION:
        raise AssertionError(f"Expected {REVISION}, found {revision}")
    source = reference_source(upstream)
    cases = fixtures()
    target = ["--target", args.rust_target] if args.rust_target else []
    print(f"Upstream: {revision}; gradient.rs has 0 upstream unit tests", flush=True)
    print(f"Rust target: {args.rust_target or 'host'}; compiler: {args.compiler}", flush=True)
    with tempfile.TemporaryDirectory(prefix=".verify-", dir=HERE) as scratch:
        directory = Path(scratch).resolve()
        assert directory.parent == HERE
        reference = directory / "reference.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_gradient_reference", "--edition=2021", *target, "-A", "warnings", "-C", "opt-level=2", "-o", reference], source=source + RUST_DRIVER)
        math_reference = directory / "math-reference.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_gradient_math", "--edition=2021", *target, "-C", "opt-level=2", "-o", math_reference], source=MATH_DRIVER)
        math_cases = ("-1.5707963267948966", "0.7853981633974483", "1e308")
        math_expected = [run([math_reference, value]) for value in math_cases]
        references = [run([reference, *map(repr, values)]) for values in cases]
        for level in ("-O0", "-O2"):
            math_binary = directory / f"math{level}.out"
            run([args.compiler, HERE / "math-probe.dl", level, "-o", math_binary])
            for value, expected in zip(math_cases, math_expected):
                actual = run([math_binary, value])
                if list(map(scalar, actual.split())) != list(map(scalar, expected.split())):
                    raise AssertionError(f"{level}: incompatible reference math runtime at {value}: {actual!r} != {expected!r}")
            print(f"{level}: native/Rust cosine runtime compatibility passed", flush=True)
            binary = directory / f"fixtures{level}.out"
            started = time.perf_counter()
            run([args.compiler, HERE / "main.dl", level, "-o", binary])
            elapsed = time.perf_counter() - started
            if run([binary]) != (HERE / "expected.txt").read_text(encoding="utf-8"):
                raise AssertionError(f"{level}: fixture output differs from expected.txt")
            print(f"{level}: independent boundary fixtures passed; compile {elapsed:.3f}s", flush=True)
            for name, pattern in (("wrong-point", "check cad gradient point"), ("wrong-angle", "the cad gradient frame of")):
                rejected = invoke([args.compiler, HERE / f"{name}.dl", level, "-o", directory / f"{name}.out"])
                diagnostic = rejected.stdout + rejected.stderr
                if rejected.returncode != 1 or "No overload matches call" not in diagnostic or pattern not in diagnostic:
                    raise AssertionError(f"{level}: unexpected {name} result {rejected.returncode}: {diagnostic}")
            print(f"{level}: both invalid type fixtures rejected", flush=True)
            driver = directory / f"differential{level}.out"
            run([args.compiler, HERE / "differential.dl", level, "-o", driver])
            for values, expected in zip(cases, references):
                compare(run([driver, *map(repr, values)]), expected, (level, values))
            print(f"{level}: {len(cases)} runtime-input differential cases passed", flush=True)
    print("All gradient checks passed.", flush=True)


if __name__ == "__main__":
    main()
