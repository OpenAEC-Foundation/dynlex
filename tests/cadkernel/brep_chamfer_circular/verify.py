#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native circular chamfering with the pinned source implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
from pathlib import Path
import runpy
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_chamfer_circular"
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


def encoded(profile, selected=(), base_face=0, base_distance=0.25, other_distance=0.25, *,
            origin=(0.0, 0.0, 0.0), radial=(1.0, 0.0, 0.0),
            height=(0.0, 0.0, 1.0), pivot=None, axis=None, angle=TAU,
            damage=0, damage_value=0.0):
    pivot = origin if pivot is None else pivot
    axis = height if axis is None else axis
    return [*origin, *radial, *height, *pivot, *axis, angle, len(profile),
            *(value for piece in profile for value in piece), len(selected), *selected,
            base_face, base_distance, other_distance, damage, damage_value]


def cases():
    cylinder = ring([(0.0, 0.0), (4.0, 0.0), (4.0, 6.0), (0.0, 6.0)])
    annulus = ring([(3.0, -2.0), (7.0, -2.0), (7.0, 3.0), (3.0, 3.0)])
    cone = ring([(0.0, 0.0), (5.0, 0.0), (0.0, 12.0)])
    torus_d = [
        arc((10.0, 0.0), 2.0, -math.pi / 2.0, math.pi / 2.0),
        line((10.0, 2.0), (10.0, 0.0)),
        line((10.0, 0.0), (10.0, -2.0)),
    ]
    sphere = [
        arc((0.0, 0.0), 4.0, -math.pi / 2.0, math.pi / 2.0),
        line((0.0, 4.0), (0.0, 0.0)),
        line((0.0, 0.0), (0.0, -4.0)),
    ]

    cylinder_adjacency = {0: (0, 1), 1: (1, 2)}
    for edge, faces in cylinder_adjacency.items():
        for base in faces:
            for first, second in ((0.05, 0.05), (0.25, 0.5), (1.0, 0.2), (2.999999999, 0.1), (3.0, 0.1), (20.0, 20.0)):
                yield f"cylinder-rim-{edge}-base-{base}-{first}-{second}", encoded(
                    cylinder, (edge,), base, first, second,
                )
    yield "cylinder-empty-selection", encoded(cylinder)
    yield "cylinder-duplicate-selection", encoded(cylinder, (0, 0), 0, 0.3, 0.4)
    yield "cylinder-both-rims", encoded(cylinder, (0, 1), 1, 0.3, 0.4)
    yield "cylinder-seam-selection", encoded(cylinder, (2,), 1, 0.3, 0.4)
    yield "cylinder-wrong-base", encoded(cylinder, (0,), 2, 0.3, 0.4)
    yield "cylinder-negative-turn", encoded(cylinder, (0,), 0, 0.3, 0.4, angle=-TAU)

    annulus_adjacency = {0: (3, 0), 1: (0, 1), 2: (1, 2), 3: (2, 3)}
    for edge, faces in annulus_adjacency.items():
        for base in faces:
            for first, second in ((0.05, 0.1), (0.4, 0.7), (1.999999999, 0.1), (2.0, 0.1), (6.0, 6.0)):
                yield f"annulus-rim-{edge}-base-{base}-{first}-{second}", encoded(
                    annulus, (edge,), base, first, second,
                )
    yield "annulus-opposite-rims", encoded(annulus, (0, 2), 3, 0.2, 0.3)
    yield "annulus-adjacent-rims", encoded(annulus, (1, 2), 1, 0.2, 0.3)
    yield "annulus-seam-selection", encoded(annulus, (4,), 0, 0.2, 0.3)
    yield "annulus-wrong-base", encoded(annulus, (1,), 3, 0.2, 0.3)

    for base in (0, 1):
        for first, second in ((0.05, 0.1), (0.5, 0.75), (4.9, 0.1), (8.0, 8.0)):
            yield f"cone-base-{base}-{first}-{second}", encoded(cone, (0,), base, first, second)
    for edge, faces in {0: (2, 0), 1: (0, 1), 2: (1, 2)}.items():
        for base in faces:
            yield f"torus-d-rim-{edge}-base-{base}", encoded(torus_d, (edge,), base, 0.15, 0.2)
    yield "sphere-is-unrecognized", encoded(sphere, (0,), 0, 0.2, 0.3)

    orientations = [
        ((512345.678, 4512345.678, 91.5), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
        ((-17.0, 23.0, 5.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
        ((3.0, -9.0, 14.0), (2 ** -0.5, -(2 ** -0.5), 0.0),
         (3 ** -0.5, 3 ** -0.5, 3 ** -0.5)),
    ]
    for index, (origin, radial, height) in enumerate(orientations):
        yield f"oriented-cylinder-{index}", encoded(
            cylinder, (1,), 2, 0.2, 0.3, origin=origin, radial=radial, height=height,
        )
        yield f"oriented-annulus-{index}", encoded(
            annulus, (2,), 2, 0.2, 0.3, origin=origin, radial=radial, height=height,
        )

    for damage in range(1, 6):
        yield f"damaged-cylinder-{damage}", encoded(
            cylinder, (0,), 0, 0.2, 0.3, damage=damage, damage_value=2.0,
        )

    rng = random.Random(0xC4A6FE2)
    for index in range(36):
        outer = rng.uniform(0.4, 15.0)
        low = rng.uniform(-12.0, 5.0)
        height = rng.uniform(0.4, 12.0)
        edge = rng.randrange(2)
        base = cylinder_adjacency[edge][rng.randrange(2)]
        first = rng.uniform(1.0e-5, min(outer, height) * 0.2)
        second = rng.uniform(1.0e-5, min(outer, height) * 0.2)
        profile = ring([(0.0, low), (outer, low), (outer, low + height), (0.0, low + height)])
        yield f"random-cylinder-{index}", encoded(profile, (edge,), base, first, second)
    for index in range(36):
        inner_low = rng.uniform(0.3, 5.0)
        inner_high = rng.uniform(0.3, 5.0)
        outer_low = inner_low + rng.uniform(0.4, 6.0)
        outer_high = inner_high + rng.uniform(0.4, 6.0)
        height = rng.uniform(0.5, 10.0)
        profile = ring([(inner_low, 0.0), (outer_low, 0.0), (outer_high, height), (inner_high, height)])
        edge = rng.randrange(4)
        base = annulus_adjacency[edge][rng.randrange(2)]
        first = rng.uniform(1.0e-5, 0.12)
        second = rng.uniform(1.0e-5, 0.12)
        yield f"random-coaxial-trapezoid-{index}", encoded(profile, (edge,), base, first, second)


def arguments(values):
    return [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]


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
    source_files = [source / "src/brep/chamfer_circular.rs", source / "src/brep/chamfer_profile.rs"]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])
    if any("#[test]" in path.read_text(encoding="utf-8") for path in source_files):
        raise AssertionError("unexpected source tests in private circular chamfer modules")

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-chamfer-circular-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    shutil.copy2(HERE / "reference.rs", output / "reference.rs")
    shutil.copy2(ROOT / "tests/cadkernel/brep_make/emit.rs", output / "emit.rs")
    shutil.copy2(source_files[0], output / "chamfer_circular_source.rs")
    shutil.copy2(source_files[1], output / "chamfer_profile_source.rs")
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
                args.rustc, "--edition=2021", output / "reference.rs",
                "--extern", f"cadkernel={library.resolve()}",
                "-L", f"dependency={args.dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected Rust diagnostics: {diagnostics}")
            diagnostics, native_seconds = process([
                args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
            ], timeout=180)
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

    count = recognized = valid = comparisons = 0
    exact_rust_optimization_parity = True
    for name, values in cases():
        if args.case not in name:
            continue
        command_arguments = arguments(values)
        outputs = {}
        raw_native = {}
        expected_limits = None
        for mode in ("O0", "O2"):
            rust_text, _ = process([binaries[mode]["rust"], *command_arguments], timeout=30)
            rust_values, limits = MEASURE["expected_values"](rust_text)
            native_text, _ = process([binaries[mode]["native"], *command_arguments], timeout=30)
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
        recognized += int(rust_o0[0])
        valid += int(rust_o0[1])
        count += 1
        if count % 25 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 0,
        "native_groups": len((HERE / "expected.txt").read_text(encoding="utf-8").splitlines()),
        "cases": count,
        "recognized_cases": recognized,
        "valid_cases": valid,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": True,
        "exact_rust_optimization_parity": exact_rust_optimization_parity,
        "numeric_rust_optimization_parity": True,
        "compile_seconds": compile_times,
        "reference_library_sha256": {
            mode: hashlib.sha256((args.libraries / f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest()
            for mode in ("O0", "O2")
        },
        "source_sha256": {
            str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in source_files
        },
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                ROOT / "lib/cadkernel/brep_chamfer_profile.dl",
                ROOT / "lib/cadkernel/brep_chamfer_circular.dl",
                ROOT / "lib/cadkernel/brep_sweep.dl",
                HERE / "main.dl", HERE / "probe.dl",
            )
        },
    }
    summary_name = "filtered-summary.json" if args.case else "summary.json"
    (output / summary_name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
