# SPDX-License-Identifier: MPL-2.0
# Source: https://github.com/HakanSeven12/cadkernel/blob/953d546b68aef4b6692566a1a9b077fc5bd9fb4f/src/geom2d/angle.rs
"""Verify the complete angle API against the pinned Rust source at O0 and O2.

Usage: python verify.py --upstream /path/to/pinned/cadkernel
Generated compiler artifacts live in a temporary directory beside this script.
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


def run(command, *, source=None):
    with unattended_child_processes():
        result = subprocess.run(
            list(map(str, command)), cwd=ROOT, input=source,
            capture_output=True, text=True, encoding="utf-8", timeout=45,
        )
    if result.returncode:
        raise AssertionError(f"{command}: exit {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


RUST_DRIVER = r'''
fn main() {
    let values: Vec<f64> = std::env::args().skip(1)
        .map(|value| value.parse().unwrap()).collect();
    let (a, s, e) = (values[0], values[1], values[2]);
    println!("{:.17e}", angle::normalize_angle(a));
    println!("{:.17e}", angle::arc_span(s, e));
    println!("{}", angle::angle_within_arc(a, s, e));
    println!("{:.17e}", angle::arc_parameter(a, s, e));
    println!("{:.17e}", angle::arc_span(angle::normalize_angle(s), angle::normalize_angle(e)));
}
'''


def fixtures():
    tau = math.tau
    angles = [
        -math.inf, -sys.float_info.max, -1e308, -1000 * tau - math.pi / 2,
        -tau, -math.pi, -2e-9, -5e-10, -1e-20, -math.ulp(0.0), -0.0,
        0.0, math.ulp(0.0), 1e-20, 5e-10, 2e-9, 0.1, math.pi / 2,
        math.pi, math.nextafter(tau, 0), tau, math.nextafter(tau, math.inf),
        1000 * tau + math.pi / 2, 1e308, sys.float_info.max, math.inf, math.nan,
    ]
    spans = [
        (0.0, math.pi), (7 * math.pi / 4, math.pi / 4), (0.0, 0.0),
        (1.0, 1.0), (0.0, tau), (-2 * tau, 3 * tau + math.pi),
        (1.0, 1.0 + 5e-10), (1.0 + 5e-10, 1.0), (1.0, 1.0 + 2e-9),
        (5e-10, tau - 5e-10), (2e-9, tau - 2e-9), (tau - 2e-10, 2e-10),
        (math.nan, 1.0), (1.0, math.nan), (math.inf, 1.0), (1.0, -math.inf),
    ]
    cases = [(angle, start, end) for start, end in spans for angle in angles]
    for start, end in spans[:12]:
        for endpoint in (start, end):
            for offset in (-2e-9, -1e-9, -5e-10, 0.0, 5e-10, 1e-9, 2e-9):
                cases.append((endpoint + offset, start, end))
    rng = random.Random(953546)
    for _ in range(300):
        cases.append(tuple(rng.uniform(-1000 * tau, 1000 * tau) for _ in range(3)))
    return cases


def scalar(value):
    if re.fullmatch(r"[+-]?nan(?:\([^)]*\))?", value, re.IGNORECASE):
        return math.nan
    return float(value)


def compare(actual, expected, context):
    got, want = actual.split(), expected.split()
    if len(got) != 5 or len(want) != 5 or got[2] != want[2]:
        raise AssertionError(f"{context}: classification/shape differs: {got} != {want}")
    for index in (0, 1, 3, 4):
        a, b = scalar(got[index]), scalar(want[index])
        if math.isnan(a) and math.isnan(b):
            continue
        if a != b or (a == 0.0 and math.copysign(1, a) != math.copysign(1, b)):
            raise AssertionError(f"{context}: result {index}: {a!r} != {b!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex"))
    parser.add_argument("--rustc", default=shutil.which("rustc") or "rustc")
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    revision = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "rev-parse", "HEAD"]).strip()
    if revision != REVISION:
        raise AssertionError(f"Expected {REVISION}, found {revision}")
    source_file = upstream / "src/geom2d/angle.rs"
    # Reject edited source even when the checkout's HEAD still matches the pin.
    pinned = run(["git", "-c", f"safe.directory={upstream.as_posix()}", "-C", upstream, "show", f"{REVISION}:src/geom2d/angle.rs"])
    if source_file.read_text(encoding="utf-8") != pinned:
        raise AssertionError("Upstream angle.rs differs from its pinned Git blob")
    source = f'#[path = "{source_file.as_posix()}"] mod angle;\n'
    cases = fixtures()
    print(f"Upstream: {revision}; compiler: {args.compiler}", flush=True)
    with tempfile.TemporaryDirectory(prefix=".verify-", dir=HERE) as scratch:
        directory = Path(scratch).resolve()
        assert directory.parent == HERE
        rust_tests = directory / "upstream-tests.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_angle_tests", "--edition=2021", "--test", "-A", "warnings", "-C", "opt-level=2", "-o", rust_tests], source=source)
        print(run([rust_tests, "--quiet"]).strip(), flush=True)
        reference = directory / "reference.out"
        run([args.rustc, "-", "--crate-name", "cadkernel_angle_reference", "--edition=2021", "-A", "warnings", "-C", "opt-level=2", "-o", reference], source=source + RUST_DRIVER)
        references = [run([reference, *map(repr, values)]) for values in cases]
        for level in ("-O0", "-O2"):
            binary = directory / f"fixtures{level}.out"
            started = time.perf_counter()
            run([args.compiler, HERE / "main.dl", level, "-o", binary])
            elapsed = time.perf_counter() - started
            expected = (HERE / "expected.txt").read_text(encoding="utf-8")
            if run([binary]) != expected:
                raise AssertionError(f"{level}: translated fixtures differ from expected.txt")
            print(f"{level}: {len(expected.splitlines())} translated/boundary assertions passed; compile {elapsed:.3f}s", flush=True)
            driver = directory / f"differential{level}.out"
            run([args.compiler, HERE / "differential.dl", level, "-o", driver])
            for values, expected in zip(cases, references):
                compare(run([driver, *map(repr, values)]), expected, (level, values))
            print(f"{level}: {len(cases)} runtime-input cases, all four APIs and nested normalization: exact numerical parity", flush=True)
    print("All angle checks passed.", flush=True)


if __name__ == "__main__":
    main()
