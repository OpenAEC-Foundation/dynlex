#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native analytic shelling with the pinned Rust implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import runpy
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_shell"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))

IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
ROTATE_XYZ = ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))
ROTATE_NEGATIVE = ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0))


def process(command, *, timeout=600):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(kind, origin, dimensions, distance, mask=0, axes=IDENTITY):
    return [kind, *origin, *dimensions, *axes[0], *axes[1], *axes[2], distance, mask]


def cases():
    box = (6.0, 5.0, 4.0)
    for mask in (0, 1, 2, 4, 8, 16, 32, 9, 18, 36, 63):
        yield f"box-positive-mask-{mask}", encoded(0, (0.0, 0.0, 0.0), box, 0.5, mask)
    for mask in (0, 1, 8, 32, 18):
        yield f"box-negative-mask-{mask}", encoded(0, (3.0, -2.0, 5.0), box, -0.5, mask, ROTATE_XYZ)
    yield "box-no-material", encoded(0, (0.0, 0.0, 0.0), (2.0, 3.0, 4.0), 1.0)
    yield "box-survey-rotated", encoded(
        0, (512345.678, 4512345.678, 91.5), box, 0.25, 32, ROTATE_NEGATIVE,
    )

    cylinder = (4.0, 8.0, 0.0)
    for mask in range(8):
        yield f"cylinder-positive-mask-{mask}", encoded(
            1, (1.0, -3.0, 2.0), cylinder, 0.75, mask,
        )
    for mask in (0, 1, 2, 4, 3, 6):
        yield f"cylinder-negative-mask-{mask}", encoded(
            1, (-4.0, 2.0, 7.0), cylinder, -0.5, mask, ROTATE_XYZ,
        )
    yield "cylinder-survey-rotated", encoded(
        1, (512345.678, 4512345.678, 91.5), cylinder, 0.5, 4, ROTATE_NEGATIVE,
    )
    yield "cylinder-no-material", encoded(1, (0.0, 0.0, 0.0), (2.0, 2.0, 0.0), 2.0)

    sphere = (4.0, 0.0, 0.0)
    yield "sphere-positive", encoded(2, (3.0, -2.0, 5.0), sphere, 1.0)
    yield "sphere-negative-rotated", encoded(2, (3.0, -2.0, 5.0), sphere, -1.0, 0, ROTATE_XYZ)
    yield "sphere-removed", encoded(2, (3.0, -2.0, 5.0), sphere, 1.0, 1)
    yield "sphere-no-material", encoded(2, (0.0, 0.0, 0.0), sphere, 4.0)
    yield "sphere-survey", encoded(2, (512345.678, 4512345.678, 91.5), sphere, 0.25)
    yield "invalid-zero", encoded(0, (0.0, 0.0, 0.0), box, 0.0)
    yield "invalid-nan", encoded(0, (0.0, 0.0, 0.0), box, math.nan)
    yield "unsupported-cone", encoded(3, (0.0, 0.0, 0.0), (3.0, 7.0, 0.0), 0.5)

    rng = random.Random(0x5E11)
    axes_values = (IDENTITY, ROTATE_XYZ, ROTATE_NEGATIVE)
    box_masks = (0, 1, 2, 4, 8, 16, 32, 9, 18, 36)
    for index in range(12):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        world = 1e9 if index % 6 == 0 else 1000.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        dimensions = tuple(scale * rng.uniform(2.5, 7.0) for _ in range(3))
        thickness = min(dimensions) * rng.uniform(0.05, 0.2)
        if index % 3 == 1:
            thickness = -thickness
        yield f"random-box-{index}", encoded(
            0, origin, dimensions, thickness, box_masks[index % len(box_masks)],
            axes_values[index % len(axes_values)],
        )

    cylinder_masks = (0, 1, 2, 4, 3, 5, 6)
    for index in range(12):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        world = 1e9 if index % 6 == 0 else 1000.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        radius = scale * rng.uniform(2.5, 7.0)
        height = scale * rng.uniform(3.0, 9.0)
        thickness = min(radius, height * 0.5) * rng.uniform(0.05, 0.2)
        if index % 3 == 1:
            thickness = -thickness
        yield f"random-cylinder-{index}", encoded(
            1, origin, (radius, height, 0.0), thickness,
            cylinder_masks[index % len(cylinder_masks)],
            axes_values[index % len(axes_values)],
        )

    for index in range(6):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        origin = tuple(rng.uniform(-1000.0, 1000.0) for _ in range(3))
        radius = scale * rng.uniform(2.0, 8.0)
        thickness = radius * rng.uniform(0.05, 0.4)
        if index % 2:
            thickness = -thickness
        yield f"random-sphere-{index}", encoded(
            2, origin, (radius, 0.0, 0.0), thickness, 0,
            axes_values[index % len(axes_values)],
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--libraries", type=Path, default=ROOT / "build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--optimization", action="append", choices=("O0", "O2"))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--run-timeout", type=float, default=600.0)
    args = parser.parse_args()
    if not math.isfinite(args.run_timeout) or args.run_timeout <= 0.0:
        parser.error("--run-timeout must be finite and positive")

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_files = [
        source / "src/brep/shell.rs", source / "src/brep/boolean.rs",
        source / "src/brep/imprint.rs", source / "src/brep/split.rs",
        source / "src/brep/classify.rs", source / "src/brep/intersect.rs",
        source / "src/brep/bounds.rs", source / "src/brep/make.rs",
        source / "src/brep/place.rs",
    ]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-shell-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ".out"
    modes = list(dict.fromkeys(args.optimization or ("O0", "O2")))
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    binaries = {}
    compile_times = {}
    if not args.skip_build:
        for mode in modes:
            library = args.libraries / f"libcadkernel-{mode}.rlib"
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
        for mode in modes:
            binaries[mode] = {
                "rust": output / f"reference-{mode}{suffix}",
                "native": output / f"native-{mode}{suffix}",
            }

    count = valid = comparisons = 0
    exact_native_optimization_parity = True
    raw_by_case = {}
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        outputs = {}
        expected_limits = None
        for mode in modes:
            rust_text, _ = process([binaries[mode]["rust"], *arguments], timeout=args.run_timeout)
            rust_values, limits = MEASURE["expected_values"](rust_text)
            native_text, _ = process([binaries[mode]["native"], *arguments], timeout=args.run_timeout)
            native_values = CURVE["parsed"](native_text)
            outputs[(mode, "rust")] = rust_values
            outputs[(mode, "native")] = native_values
            raw_by_case[(name, mode)] = native_text
            expected_limits = limits
        if len(modes) == 2 and raw_by_case[(name, modes[0])] != raw_by_case[(name, modes[1])]:
            exact_native_optimization_parity = False
        for mode in modes:
            native = outputs[(mode, "native")]
            rust = outputs[(mode, "rust")]
            if len(native) != len(rust):
                raise AssertionError(f"{name}/{mode}: {len(native)} native fields != {len(rust)} Rust fields")
            for field, (actual, expected, limit) in enumerate(zip(native, rust, expected_limits)):
                if not CURVE["equal"](actual, expected, exact=limit < 0, absolute=max(limit, 5e-9)):
                    raise AssertionError(f"{name}/{mode} field {field}: native={actual!r}, Rust={expected!r}")
            comparisons += len(rust)
        valid += int(outputs[(modes[0], "rust")][1])
        count += 1
        if count % 10 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "cases": count,
        "valid_cases": valid,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": exact_native_optimization_parity,
        "compile_seconds": compile_times,
        "source_sha256": {
            str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in source_files
        },
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                ROOT / "lib/cadkernel/brep_shell.dl",
                ROOT / "lib/cadkernel/brep_split.dl",
                ROOT / "lib/cadkernel/brep_boolean.dl",
                HERE / "probe.dl", HERE / "main.dl",
            )
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
