#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native edge splitting with the pinned Rust implementation."""
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
HERE = ROOT / "tests/cadkernel/brep_split"
CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = (CROSS[key] for key in ("CURVE", "MEASURE"))


def process(command, *, timeout=300):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(kind=0, parameters=(2.0, 4.0, 6.0), origin=(0.0, 0.0, 0.0),
            edge=0, fraction=0.5, point=False, repeat=0, second=0.5):
    return [kind, *parameters, *origin, edge, fraction, float(point), repeat, second]


def encoded_face(parameters=(2.0, 4.0, 6.0), origin=(0.0, 0.0, 0.0),
                 style=0, fraction=0.5, tolerance=1e-9,
                 repeat=False, second=0.75):
    return [2, *parameters, *origin, style, fraction, tolerance, float(repeat), second]


def encoded_sphere_face(radius=5.0, origin=(0.0, 0.0, 0.0),
                        fraction=0.2, tolerance=1e-9, repeat=False):
    return [3, radius, 0.0, 0.0, *origin, 0, fraction, tolerance, float(repeat), 0.0]


def encoded_circle_face(parameters=(8.0, 4.0, 2.0), origin=(0.0, 0.0, 0.0),
                        radius=3.0, tolerance=1e-9, repeat=False):
    return [4, *parameters, *origin, 0, radius, tolerance, float(repeat), 0.0]


def encoded_periodic_band(radius=5.0, height=8.0, origin=(0.0, 0.0, 0.0),
                          fraction=0.5, tolerance=1e-9,
                          repeat=False, second=0.75):
    return [5, radius, height, 0.0, *origin, 0, fraction, tolerance, float(repeat), second]


def encoded_ellipse_face(parameters=(8.0, 4.0, 2.0), origin=(0.0, 0.0, 0.0),
                         major=3.0, minor=1.5, tolerance=1e-9,
                         repeat=False):
    return [6, *parameters, *origin, 0, major, tolerance, float(repeat), minor]


def cases():
    for edge in range(12):
        for fraction in (0.1, 0.35, 0.5, 0.9):
            for point in (False, True):
                yield f"box-edge-{edge}-fraction-{fraction}-point-{point}", encoded(
                    edge=edge, fraction=fraction, point=point,
                )
    for edge in range(3):
        for fraction in (0.1, 0.25, 0.5, 0.75, 0.9):
            for point in (False, True):
                yield f"cylinder-edge-{edge}-fraction-{fraction}-point-{point}", encoded(
                    kind=1, parameters=(3.0, 5.0, 0.0), edge=edge,
                    fraction=fraction, point=point,
                )
    for fraction in (-1.0, -1e-12, 0.0, 1e-12, 1.0 - 1e-12, 1.0, 2.0, math.nan):
        yield f"refused-box-{fraction!r}", encoded(fraction=fraction)
        yield f"refused-cylinder-{fraction!r}", encoded(
            kind=1, parameters=(3.0, 5.0, 0.0), fraction=fraction,
        )
    for edge in range(12):
        for repeat in (1, 2):
            yield f"box-repeat-edge-{edge}-side-{repeat}", encoded(
                edge=edge, fraction=0.6, repeat=repeat, second=0.4,
            )
    for edge in range(3):
        for repeat in (1, 2):
            yield f"cylinder-repeat-edge-{edge}-side-{repeat}", encoded(
                kind=1, parameters=(3.0, 5.0, 0.0), edge=edge,
                fraction=0.6, repeat=repeat, second=0.4,
            )

    for style in (0, 1):
        for fraction in (0.1, 0.25, 0.5, 0.75, 0.9):
            yield f"face-style-{style}-fraction-{fraction}", encoded_face(
                style=style, fraction=fraction,
            )
    for parameters in ((4.0, 4.0, 4.0), (2.0, 4.0, 6.0), (9.0, 3.0, 2.0)):
        yield f"face-diagonal-{parameters}", encoded_face(
            parameters=parameters, style=2,
        )
    yield "face-miss", encoded_face(style=3, fraction=1.0)
    yield "face-off-surface", encoded_face(style=4, fraction=0.5)
    for first, second in ((0.2, 0.8), (0.7, 0.3), (0.25, 0.5), (0.5, 0.75)):
        yield f"face-repeat-{first}-{second}", encoded_face(
            parameters=(9.0, 9.0, 9.0), fraction=first,
            repeat=True, second=second,
        )
    yield "face-survey", encoded_face(
        origin=(512345.678, 4512345.678, 91.5), tolerance=1e-6,
    )

    for fraction in (-0.8, -0.3, 0.0, 0.2, 0.8):
        yield f"sphere-face-{fraction}", encoded_sphere_face(
            fraction=fraction, repeat=True,
        )
    yield "sphere-face-survey", encoded_sphere_face(
        radius=125.0, origin=(512345.678, 4512345.678, 91.5),
        fraction=0.4, tolerance=1e-6, repeat=True,
    )
    for index, radius in enumerate((1e-3, 0.25, 17.0, 1e4)):
        yield f"sphere-face-scale-{index}", encoded_sphere_face(
            radius=radius,
            origin=(3.0 * radius, -2.0 * radius, 5.0 * radius),
            fraction=(-0.6 + 0.4 * index),
            tolerance=max(1e-12, radius * 1e-9),
            repeat=bool(index % 2),
        )

    for radius, repeat in ((1.0, False), (2.0, False), (3.0, False), (3.0, True)):
        yield f"circle-face-radius-{radius}-repeat-{repeat}", encoded_circle_face(
            radius=radius, repeat=repeat,
        )
    yield "circle-face-eight-crossings", encoded_circle_face(
        parameters=(4.0, 4.0, 2.0), radius=3.0, repeat=True,
    )
    yield "circle-face-survey", encoded_circle_face(
        parameters=(800.0, 400.0, 50.0),
        origin=(512345.678, 4512345.678, 91.5),
        radius=300.0, tolerance=1e-6, repeat=True,
    )

    for first, repeat, second in ((0.2, False, 0.75), (0.5, True, 0.75), (0.8, True, 0.25)):
        yield f"periodic-band-{first}-repeat-{repeat}-{second}", encoded_periodic_band(
            fraction=first, repeat=repeat, second=second,
        )
    yield "periodic-band-survey", encoded_periodic_band(
        radius=125.0, height=300.0,
        origin=(512345.678, 4512345.678, 91.5),
        fraction=0.35, tolerance=1e-6, repeat=True, second=0.8,
    )

    for major, minor, repeat in (
        (2.0, 1.0, False),
        (3.5, 2.0, False),
        (5.0, 3.0, False),
        (5.0, 3.0, True),
    ):
        yield f"ellipse-face-{major}-{minor}-repeat-{repeat}", encoded_ellipse_face(
            major=major, minor=minor, repeat=repeat,
        )
    yield "ellipse-face-survey", encoded_ellipse_face(
        parameters=(800.0, 400.0, 50.0),
        origin=(512345.678, 4512345.678, 91.5),
        major=500.0, minor=300.0, tolerance=1e-6, repeat=True,
    )

    rng = random.Random(0x5A117)
    for index in range(64):
        scale = 10.0 ** rng.uniform(-4.0, 5.0)
        world = 1e9 if index % 7 == 0 else 100.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        kind = index % 2
        if kind == 0:
            parameters = tuple(scale * rng.uniform(0.5, 8.0) for _ in range(3))
            edges = 12
        else:
            parameters = (scale * rng.uniform(0.5, 5.0), scale * rng.uniform(0.5, 8.0), 0.0)
            edges = 3
        yield f"random-{index}", encoded(
            kind=kind,
            parameters=parameters,
            origin=origin,
            edge=rng.randrange(edges),
            fraction=rng.uniform(0.02, 0.98),
            point=bool(index % 3),
            repeat=(index % 3),
            second=rng.uniform(0.05, 0.95),
        )

    face_rng = random.Random(0xFACE517)
    for index in range(48):
        scale = 10.0 ** face_rng.uniform(-4.0, 5.0)
        world = 1e9 if index % 7 == 0 else 100.0
        origin = tuple(face_rng.uniform(-world, world) for _ in range(3))
        parameters = tuple(scale * face_rng.uniform(0.5, 8.0) for _ in range(3))
        tolerance = max(1e-9, scale * 1e-8, world * 5e-15)
        style = index % 3
        first = face_rng.uniform(0.05, 0.95)
        second = face_rng.uniform(0.05, 0.95)
        if abs(first - second) < 0.1:
            second = 0.95 if first < 0.5 else 0.05
        yield f"face-random-{index}", encoded_face(
            parameters=parameters,
            origin=origin,
            style=style,
            fraction=first,
            tolerance=tolerance,
            repeat=(style != 2 and index % 4 == 0),
            second=second,
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
    parser.add_argument("--run-timeout", type=float, default=300.0)
    parser.add_argument("--optimization", action="append", choices=("O0", "O2"))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_test_names = re.findall(
        r"#\[test\]\s*fn (\w+)",
        (source / "src/brep/split.rs").read_text(encoding="utf-8"),
    )
    expected_test_names = (HERE / "source-tests.txt").read_text(encoding="utf-8").splitlines()
    if source_test_names != expected_test_names or len(source_test_names) != 23:
        raise AssertionError("the pinned split.rs test inventory changed")
    source_files = [source / "src/brep/split.rs", source / "src/brep/make.rs"]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-split-", dir=ROOT / "build"))
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
        valid += int(outputs[(modes[0], "rust")][0])
        count += 1
        if count % 25 == 0:
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
            for path in (ROOT / "lib/cadkernel/brep_split.dl", HERE / "probe.dl", HERE / "main.dl")
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
