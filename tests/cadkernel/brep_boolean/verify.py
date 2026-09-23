#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native planar-solid booleans with the pinned Rust implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import runpy
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_boolean"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))


def process(command, *, timeout=600):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(operation=0, first_origin=(0.0, 0.0, 0.0),
            first_size=(10.0, 10.0, 10.0), second_origin=(5.0, 5.0, 5.0),
            second_size=(10.0, 10.0, 10.0), tolerance=1e-9, shape=0):
    return [*first_origin, *first_size, *second_origin, *second_size,
            operation, tolerance, shape]


def cases():
    yield "door-cut", encoded(operation=2, shape=3)
    for operation in range(3):
        yield f"overlap-operation-{operation}", encoded(operation)

    for case_index, (base, height) in enumerate((
        ((10.0, 5.0, 0.0), 10.0),
        ((10.0, 10.0, 0.0), 10.0),
        ((10.0, 5.0, -2.0), 14.0),
        ((9.0, 5.0, -2.0), 14.0),
    )):
        for operation in range(3):
            yield f"box-cylinder-{case_index}-operation-{operation}", encoded(
                operation,
                second_origin=base,
                second_size=(3.0, height, 0.0),
                shape=2,
            )

    for operation in range(3):
        yield f"stacked-operation-{operation}", encoded(
            operation,
            second_origin=(0.0, 0.0, 10.0),
        )
        yield f"identical-operation-{operation}", encoded(
            operation,
            first_size=(4.0, 4.0, 4.0),
            second_origin=(0.0, 0.0, 0.0),
            second_size=(4.0, 4.0, 4.0),
        )
    for operation in (0, 2):
        yield f"partial-wall-operation-{operation}", encoded(
            operation,
            second_origin=(0.0, 0.0, 10.0),
            second_size=(4.0, 4.0, 4.0),
        )

    topology_cases = (
        ("disjoint", (0.0, 0.0, 0.0), (2.0, 2.0, 2.0),
         (5.0, 5.0, 5.0), (2.0, 2.0, 2.0)),
        ("contained", (0.0, 0.0, 0.0), (10.0, 10.0, 10.0),
         (2.0, 2.0, 2.0), (6.0, 6.0, 6.0)),
        ("reverse-contained", (2.0, 2.0, 2.0), (6.0, 6.0, 6.0),
         (0.0, 0.0, 0.0), (10.0, 10.0, 10.0)),
    )
    for label, first_origin, first_size, second_origin, second_size in topology_cases:
        for operation in range(3):
            yield f"{label}-operation-{operation}", encoded(
                operation, first_origin, first_size, second_origin, second_size,
            )
    sphere_cases = (
        ("sphere-contained", (3.0, -2.0, 5.0), 4.0,
         (3.0, -2.0, 5.0), 3.0),
        ("sphere-reverse-contained", (3.0, -2.0, 5.0), 3.0,
         (3.0, -2.0, 5.0), 4.0),
        ("sphere-disjoint", (-8.0, 1.0, 2.0), 2.5,
         (8.0, 1.0, 2.0), 3.0),
        ("sphere-survey-contained", (512345.678, 4512345.678, 91.5), 4.0,
         (512345.678, 4512345.678, 91.5), 3.0),
    )
    for label, first_origin, first_radius, second_origin, second_radius in sphere_cases:
        for operation in range(3):
            yield f"{label}-operation-{operation}", encoded(
                operation,
                first_origin, (first_radius, first_radius, first_radius),
                second_origin, (second_radius, second_radius, second_radius),
                1e-6 if "survey" in label else 1e-9,
                1,
            )
    origin = (512345.678, 4512345.678, 91.5)
    for operation in range(3):
        yield f"survey-operation-{operation}", encoded(
            operation,
            first_origin=origin,
            second_origin=tuple(value + 5.0 for value in origin),
            tolerance=1e-6,
        )

    rng = random.Random(0xB001EA4)
    for index in range(48):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        world = 1e9 if index % 11 == 0 else 100.0
        first_origin = tuple(rng.uniform(-world, world) for _ in range(3))
        first_size = tuple(scale * rng.uniform(1.0, 5.0) for _ in range(3))
        second_origin = tuple(
            first_origin[axis] + first_size[axis] * rng.uniform(0.1, 0.9)
            for axis in range(3)
        )
        second_size = tuple(scale * rng.uniform(1.0, 5.0) for _ in range(3))
        tolerance = max(1e-9, scale * 1e-8, world * 5e-15)
        yield f"random-{index}", encoded(
            index % 3, first_origin, first_size, second_origin, second_size,
            tolerance,
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
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_files = [
        source / "src/brep/boolean.rs", source / "src/brep/imprint.rs",
        source / "src/brep/split.rs", source / "src/brep/classify.rs",
        source / "src/brep/intersect.rs", source / "src/brep/bounds.rs",
        source / "src/brep/make.rs", source / "src/brep/sweep.rs",
    ]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-boolean-", dir=ROOT / "build"))
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
            rust_text, _ = process([binaries[mode]["rust"], *arguments], timeout=120)
            rust_values, limits = MEASURE["expected_values"](rust_text)
            native_text, _ = process([binaries[mode]["native"], *arguments], timeout=120)
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
        valid += int(outputs[(modes[0], "rust")][0])
        count += 1
        if count % 15 == 0:
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
            for path in (ROOT / "lib/cadkernel/brep_boolean.dl", HERE / "probe.dl", HERE / "main.dl")
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
