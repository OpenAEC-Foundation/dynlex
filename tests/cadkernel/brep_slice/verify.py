#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native plane slicing with the pinned Rust implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import runpy
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_slice"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))


def process(command, *, timeout=600):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(origin=(0.0, 0.0, 0.0), size=(4.0, 2.0, 2.0),
            plane_origin=(1.5, 0.0, 0.0), x_axis=(0.0, 1.0, 0.0),
            normal=(1.0, 0.0, 0.0), mode=0):
    return [*origin, *size, *plane_origin, *x_axis, *normal, mode]


def cases():
    yield "source-crossing", encoded()
    yield "source-tangent", encoded(plane_origin=(4.0, 0.0, 0.0))
    yield "outside-positive", encoded(plane_origin=(-1.0, 0.0, 0.0))
    yield "outside-negative", encoded(plane_origin=(5.0, 0.0, 0.0))
    yield "crossing-reversed", encoded(
        plane_origin=(1.5, 0.0, 0.0), normal=(-1.0, 0.0, 0.0),
    )
    yield "crossing-y", encoded(
        plane_origin=(0.0, 0.75, 0.0), x_axis=(0.0, 0.0, 1.0),
        normal=(0.0, 1.0, 0.0),
    )
    yield "crossing-z", encoded(
        plane_origin=(0.0, 0.0, 0.5), x_axis=(1.0, 0.0, 0.0),
        normal=(0.0, 0.0, 1.0),
    )
    yield "diagonal", encoded(
        plane_origin=(2.0, 1.0, 0.0), x_axis=(0.0, 0.0, 1.0),
        normal=(1.0, 1.0, 0.0),
    )
    survey = (512345.678, 4512345.678, 91.5)
    yield "survey", encoded(
        origin=survey,
        plane_origin=(survey[0] + 1.5, survey[1], survey[2]),
    )
    yield "invalid-frame", encoded(x_axis=(1.0, 0.0, 0.0))
    yield "sheet-plane-x", encoded(mode=1)
    yield "sheet-plane-y", encoded(
        plane_origin=(0.0, 0.75, 0.0), x_axis=(0.0, 0.0, 1.0),
        normal=(0.0, 1.0, 0.0), mode=1,
    )
    yield "sheet-plane-diagonal", encoded(
        plane_origin=(2.0, 1.0, 0.0), x_axis=(0.0, 0.0, 1.0),
        normal=(1.0, 1.0, 0.0), mode=1,
    )
    yield "surface-plane", encoded(mode=2)
    yield "surface-plane-reversed", encoded(mode=3)
    yield "sheet-surface", encoded(mode=4)

    surface_points = {
        0: ((0.0, 0.0, -2.0), (0.0, 0.0, 0.0), (0.0, 0.0, 3.0)),
        1: ((1.0, 0.0, 0.0), (3.0, 0.0, 2.0), (5.0, 0.0, -1.0)),
        2: ((1.0, 0.0, 0.0), (3.0, 0.0, 0.0), (5.0, 0.0, 2.0)),
        3: ((1.0, 0.0, 0.0), (3.0, 0.0, 0.0), (5.0, 0.0, 0.0)),
        4: ((3.0, 0.0, 0.0), (5.0, 0.0, 0.0), (7.0, 0.0, 0.0)),
    }
    for kind, points in surface_points.items():
        for reversed_face in (False, True):
            mode = 5 + kind + (5 if reversed_face else 0)
            for point_index, point in enumerate(points):
                yield f"surface-side-{kind}-reversed-{reversed_face}-point-{point_index}", encoded(
                    size=(3.0, 0.4 if kind == 2 else 2.0, 1.0),
                    plane_origin=point,
                    x_axis=(1.0, 0.0, 0.0),
                    normal=(0.0, 0.0, 1.0),
                    mode=mode,
                )

    for mode, name in ((15, "cylinder"), (16, "cone"), (17, "sphere"), (18, "torus-refusal")):
        yield f"curved-surface-{name}", encoded(
            origin=(-4.0, -4.0, -4.0),
            size=(8.0, 8.0, 8.0),
            plane_origin=(0.0, 0.0, 0.0),
            x_axis=(1.0, 0.0, 0.0),
            normal=(0.0, 0.0, 1.0),
            mode=mode,
        )

    rng = random.Random(0x511CE)
    axes = (
        ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
        ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
        ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
    )
    for index in range(54):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        world = 1e9 if index % 13 == 0 else 100.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        size = tuple(scale * rng.uniform(1.0, 5.0) for _ in range(3))
        normal, x_axis = axes[index % len(axes)]
        if index % 7 == 0:
            normal = tuple(-value for value in normal)
        axis = index % 3
        point = list(origin)
        point[axis] += size[axis] * rng.uniform(0.1, 0.9)
        yield f"random-axis-{index}", encoded(
            origin, size, tuple(point), x_axis, normal,
        )

    for index in range(12):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        origin = tuple(rng.uniform(-1000.0, 1000.0) for _ in range(3))
        size = tuple(scale * rng.uniform(1.0, 4.0) for _ in range(3))
        centre = tuple(origin[axis] + size[axis] * 0.5 for axis in range(3))
        angle = rng.uniform(0.2, 1.3)
        normal = (math.cos(angle), math.sin(angle), 0.0)
        yield f"random-oblique-{index}", encoded(
            origin, size, centre, (0.0, 0.0, 1.0), normal,
        )

    for index in range(18):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        world = 1e9 if index % 9 == 0 else 100.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        size = tuple(scale * rng.uniform(1.0, 4.0) for _ in range(3))
        axis = index % 3
        normal, x_axis = axes[axis]
        point = list(origin)
        point[axis] += size[axis] * rng.uniform(0.15, 0.85)
        mode = 3 if index % 5 == 0 else 2
        yield f"random-surface-{index}", encoded(
            origin, size, tuple(point), x_axis, normal, mode,
        )

    for index in range(12):
        scale = 10.0 ** rng.uniform(-2.0, 3.0)
        origin = tuple(rng.uniform(-1000.0, 1000.0) for _ in range(3))
        size = (scale * rng.uniform(1.0, 4.0), scale * rng.uniform(1.0, 4.0), scale)
        choose_x = index % 2 == 0
        axis = 0 if choose_x else 1
        point = list(origin)
        point[axis] += size[axis] * rng.uniform(0.15, 0.85)
        normal, x_axis = axes[axis]
        yield f"random-sheet-{index}", encoded(
            origin, size, tuple(point), x_axis, normal, 1,
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
    source_test_names = re.findall(
        r"#\[test\]\s*fn (\w+)",
        (source / "src/brep/slice.rs").read_text(encoding="utf-8"),
    )
    expected_test_names = (HERE / "source-tests.txt").read_text(encoding="utf-8").splitlines()
    if source_test_names != expected_test_names or len(source_test_names) != 2:
        raise AssertionError("the pinned slice.rs test inventory changed")
    source_files = [
        source / "src/brep/slice.rs", source / "src/brep/boolean.rs",
        source / "src/brep/imprint.rs", source / "src/brep/split.rs",
        source / "src/brep/classify.rs", source / "src/brep/intersect.rs",
        source / "src/brep/bounds.rs", source / "src/brep/make.rs",
        source / "src/brep/place.rs",
    ]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-slice-", dir=ROOT / "build"))
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

    count = valid = divided = comparisons = 0
    exact_native_optimization_parity = True
    raw_by_case = {}
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") for value in values]
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
        divided += int(outputs[(modes[0], "rust")][1])
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
        "divided_cases": divided,
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
            for path in (ROOT / "lib/cadkernel/brep_slice.dl", HERE / "probe.dl", HERE / "main.dl")
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
