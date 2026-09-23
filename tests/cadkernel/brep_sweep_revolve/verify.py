#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native full-turn solid revolution with the pinned Rust kernel."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
from pathlib import Path
import runpy
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_sweep_revolve"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))
TAU = math.tau


def process(command, *, timeout=240):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def line(first, last):
    return [0, *first, *last]


def arc(centre, radius, start, end):
    return [2, *centre, radius, start, end]


def ring(points):
    return [line(points[index], points[(index + 1) % len(points)]) for index in range(len(points))]


def reversed_profile(profile):
    result = []
    for piece in reversed(profile):
        if piece[0] == 0:
            result.append(line(piece[3:5], piece[1:3]))
        else:
            result.append(piece[:])
    return result


def encoded(profile, *, origin=(0.0, 0.0, 0.0), radial=(1.0, 0.0, 0.0),
            height=(0.0, 0.0, 1.0), pivot=None, axis=None, angle=TAU):
    pivot = origin if pivot is None else pivot
    axis = height if axis is None else axis
    return [*origin, *radial, *height, *pivot, *axis, angle, len(profile),
            *(value for piece in profile for value in piece)]


def cases():
    cylinder = ring([(0.0, 0.0), (3.0, 0.0), (3.0, 6.0), (0.0, 6.0)])
    cone = ring([(0.0, 0.0), (5.0, 0.0), (0.0, 12.0)])
    annulus = ring([(4.0, 0.0), (7.0, 0.0), (7.0, 2.0), (4.0, 2.0)])
    sphere = [
        arc((0.0, 0.0), 4.0, -math.pi / 2.0, math.pi / 2.0),
        line((0.0, 4.0), (0.0, 0.0)), line((0.0, 0.0), (0.0, -4.0)),
    ]
    torus_d = [
        arc((10.0, 0.0), 2.0, -math.pi / 2.0, math.pi / 2.0),
        line((10.0, 2.0), (10.0, 0.0)), line((10.0, 0.0), (10.0, -2.0)),
    ]
    torus_circle = [
        arc((8.0, 0.0), 1.5, 0.0, math.pi),
        arc((8.0, 0.0), 1.5, math.pi, TAU),
    ]
    baselines = {
        "cylinder": cylinder, "cone": cone, "annulus": annulus,
        "sphere": sphere, "torus-d": torus_d, "torus-circle": torus_circle,
    }
    for name, profile in baselines.items():
        yield name, encoded(profile)
        yield f"{name}-negative-turn", encoded(profile, angle=-TAU)
        if all(piece[0] == 0 for piece in profile):
            yield f"{name}-reverse", encoded(reversed_profile(profile))

    orientations = [
        ((512345.678, 4512345.678, 91.5), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
        ((-17.0, 23.0, 5.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
        ((3.0, -9.0, 14.0), (2 ** -0.5, -(2 ** -0.5), 0.0),
         (3 ** -0.5, 3 ** -0.5, 3 ** -0.5)),
    ]
    for index, (origin, radial, height) in enumerate(orientations):
        yield f"oriented-cylinder-{index}", encoded(cylinder, origin=origin, radial=radial, height=height)
        shifted = tuple(origin[axis] + 3.25 * height[axis] for axis in range(3))
        yield f"oriented-annulus-pivot-{index}", encoded(
            annulus, origin=origin, radial=radial, height=height, pivot=shifted,
        )

    yield "straddles-axis", encoded(ring([(-2.0, 0.0), (3.0, 0.0), (3.0, 4.0), (-2.0, 4.0)]))
    yield "far-side", encoded(ring([(-7.0, 0.0), (-4.0, 0.0), (-4.0, 2.0), (-7.0, 2.0)]))
    yield "open-profile", encoded(cylinder[:3])
    yield "zero-area", encoded(ring([(0.0, 0.0), (4.0, 0.0), (1.0, 0.0)]))
    yield "unsupported-circle", encoded([[1, 4.0, 2.0, 1.0]])
    yield "axis-off-plane", encoded(annulus, axis=(0.0, 1.0, 1.0))
    yield "pivot-off-plane", encoded(annulus, pivot=(0.0, 5.0, 0.0))
    yield "zero-axis", encoded(annulus, axis=(0.0, 0.0, 0.0))
    yield "zero-angle", encoded(annulus, angle=0.0)
    yield "too-large-angle", encoded(annulus, angle=TAU + 1e-10)

    rng = random.Random(0xD1A6E)
    for index in range(48):
        inner = rng.uniform(0.0, 8.0)
        outer = inner + rng.uniform(0.05, 6.0)
        low = rng.uniform(-20.0, 10.0)
        high = low + rng.uniform(0.05, 12.0)
        profile = ring([(inner, low), (outer, low), (outer, high), (inner, high)])
        yield f"random-rectangle-{index}", encoded(profile)
    for index in range(36):
        inner_low = rng.uniform(0.0, 5.0)
        inner_high = rng.uniform(0.0, 5.0)
        outer_low = inner_low + rng.uniform(0.1, 5.0)
        outer_high = inner_high + rng.uniform(0.1, 5.0)
        height = rng.uniform(0.1, 10.0)
        profile = ring([(inner_low, 0.0), (outer_low, 0.0), (outer_high, height), (inner_high, height)])
        yield f"random-trapezoid-{index}", encoded(profile)
    for index in range(30):
        major = rng.uniform(2.0, 20.0)
        minor = rng.uniform(0.05, major * 0.45)
        centre_height = rng.uniform(-10.0, 10.0)
        profile = [
            arc((major, centre_height), minor, -math.pi / 2.0, math.pi / 2.0),
            line((major, centre_height + minor), (major, centre_height)),
            line((major, centre_height), (major, centre_height - minor)),
        ]
        yield f"random-torus-d-{index}", encoded(profile)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument(
        "--rustc", type=Path,
        default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe",
    )
    parser.add_argument("--dependencies", type=Path, default=ROOT / "build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--libraries", type=Path, default=ROOT / "build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_file = source / "src/brep/sweep.rs"
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", source_file.relative_to(source)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-sweep-revolve-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    binaries = {}
    compile_times = {}
    if not args.skip_build:
        for mode in ("O0", "O2"):
            library = args.libraries / f"libcadkernel-{mode}.rlib"
            if not library.is_file():
                raise AssertionError(f"missing reference library: {library}")
            reference = output / f"reference-{mode}{suffix}"
            native = output / f"native-{mode}{suffix}"
            diagnostics, rust_seconds = process([
                args.rustc, "--edition=2021", HERE / "reference.rs",
                "--extern", f"cadkernel={library.resolve()}",
                "-L", f"dependency={args.dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected Rust diagnostics: {diagnostics}")
            diagnostics, native_seconds = process([
                args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
            binaries[mode] = {"rust": reference, "native": native}
            compile_times[mode] = {"rust": rust_seconds, "native": native_seconds}
            print(f"{mode}: Rust={rust_seconds:.3f}s native={native_seconds:.3f}s", flush=True)
    else:
        for mode in ("O0", "O2"):
            binaries[mode] = {
                "rust": output / f"reference-{mode}{suffix}",
                "native": output / f"native-{mode}{suffix}",
            }
            if not all(path.is_file() for path in binaries[mode].values()):
                raise AssertionError(f"missing --skip-build binaries for {mode}")
    if args.build_only:
        return

    count = valid = comparisons = 0
    exact_rust_optimization_parity = True
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        outputs = {}
        raw_native = {}
        expected_limits = None
        for mode in ("O0", "O2"):
            rust_text, _ = process([binaries[mode]["rust"], *arguments], timeout=30)
            rust_values, limits = MEASURE["expected_values"](rust_text)
            native_text, _ = process([binaries[mode]["native"], *arguments], timeout=30)
            native_values = CURVE["parsed"](native_text)
            outputs[(mode, "rust")] = rust_values
            outputs[(mode, "native")] = native_values
            raw_native[mode] = native_text
            expected_limits = limits
        if raw_native["O0"] != raw_native["O2"]:
            raise AssertionError(f"{name}: native optimization mismatch")
        rust_o0, rust_o2 = outputs[("O0", "rust")], outputs[("O2", "rust")]
        if len(rust_o0) != len(rust_o2) or not all(CURVE["equal"](a, b, exact=True) for a, b in zip(rust_o0, rust_o2)):
            exact_rust_optimization_parity = False
            if len(rust_o0) != len(rust_o2) or not all(CURVE["equal"](a, b, exact=False) for a, b in zip(rust_o0, rust_o2)):
                raise AssertionError(f"{name}: Rust optimization divergence")
        for mode in ("O0", "O2"):
            native = outputs[(mode, "native")]
            rust = outputs[(mode, "rust")]
            if len(native) != len(rust):
                raise AssertionError(f"{name}/{mode}: {len(native)} native fields != {len(rust)} Rust fields")
            for field, (actual, expected, limit) in enumerate(zip(native, rust, expected_limits)):
                if not CURVE["equal"](actual, expected, exact=limit < 0, absolute=max(limit, 5e-10)):
                    raise AssertionError(f"{name}/{mode} field {field}: native={actual!r}, Rust={expected!r}")
            comparisons += len(rust)
        valid += int(rust_o0[0])
        count += 1
        if count % 25 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 20,
        "native_groups": len((HERE / "expected.txt").read_text(encoding="utf-8").splitlines()),
        "cases": count,
        "valid_cases": valid,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": True,
        "exact_rust_optimization_parity": exact_rust_optimization_parity,
        "compile_seconds": compile_times,
        "reference_library_sha256": {
            mode: hashlib.sha256((args.libraries / f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest()
            for mode in ("O0", "O2")
        },
        "source_sha256": {str(source_file.relative_to(source)): hashlib.sha256(source_file.read_bytes()).hexdigest()},
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_sweep.dl", HERE / "main.dl", HERE / "probe.dl")
        },
    }
    summary_name = "filtered-summary.json" if args.case else "summary.json"
    (output / summary_name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
