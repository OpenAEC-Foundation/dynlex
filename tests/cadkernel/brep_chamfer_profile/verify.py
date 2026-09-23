#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native profile chamfering with the pinned source implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_chamfer_profile"


def process(command, *, timeout=180):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def line(start, end):
    return (0, start[0], start[1], end[0], end[1], 0.0)


def arc(centre, radius, start, end):
    return (1, centre[0], centre[1], radius, start, end)


def circle(centre, radius):
    return (2, centre[0], centre[1], radius, 0.0, 0.0)


def record(curve, face=0, edge=-1):
    return (*curve, face, edge)


def encoded(records, *, points=None, faces=None, base=1.0, other=1.0,
            tolerance=1e-9, avoid_axis=False):
    count = len(records)
    values = [
        count,
        count if points is None else points,
        count if faces is None else faces,
        base,
        other,
        tolerance,
        int(avoid_axis),
    ]
    for item in records:
        values.extend(item)
    return values


def polygon_records(points, *, faces=None, selections=None):
    count = len(points)
    faces = [0] * count if faces is None else faces
    selections = {} if selections is None else selections
    return [
        record(line(points[index], points[(index + 1) % count]),
               faces[index], selections.get(index, -1))
        for index in range(count)
    ]


def rectangle(origin=(0.0, 0.0), size=(10.0, 10.0), *, selections=None,
              faces=None, base=2.0, other=1.0, tolerance=1e-9,
              avoid_axis=False):
    x, y = origin
    width, height = size
    points = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
    if selections is None:
        selections = {0: 1}
    if faces is None:
        faces = [1, 1, 1, 0]
    return encoded(
        polygon_records(points, faces=faces, selections=selections),
        base=base, other=other, tolerance=tolerance, avoid_axis=avoid_axis,
    )


def transformed(points, angle, scale, offset):
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return [
        (
            offset[0] + scale * (cosine * x - sine * y),
            offset[1] + scale * (sine * x + cosine * y),
        )
        for x, y in points
    ]


def cases():
    yield "rectangle-single", rectangle()
    yield "rectangle-none", rectangle(selections={}, faces=[0, 0, 0, 0])
    yield "rectangle-opposite", rectangle(
        selections={0: 1, 2: 3}, faces=[1, 0, 1, 0], base=1.5, other=0.5,
    )
    yield "invalid-two-points", encoded(
        polygon_records([(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]), points=2,
    )
    yield "invalid-short-face-list", encoded(
        polygon_records([(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]), faces=2,
    )
    yield "missing-adjacent-face", rectangle(faces=[1, 1, 1, -1])
    yield "same-adjacent-face", rectangle(faces=[0, 0, 0, 0])
    yield "distance-removes-edge", rectangle(base=10.0)
    yield "distance-at-limit", rectangle(base=10.0 - 1e-9)
    yield "distance-below-limit", rectangle(base=math.nextafter(10.0 - 1e-9, 0.0))
    yield "axis-contact-allowed", rectangle(avoid_axis=False)
    yield "axis-contact-refused", rectangle(avoid_axis=True)
    yield "axis-above-tolerance", rectangle(origin=(1.1e-9, 0.0), avoid_axis=True)
    yield "axis-at-tolerance", rectangle(origin=(1e-9, 0.0), avoid_axis=True)
    yield "negative-distance", rectangle(base=-0.5, other=0.25)
    yield "zero-distance", rectangle(base=0.0, other=0.0)

    bow = [(0.0, 0.0), (4.0, 4.0), (0.0, 4.0), (4.0, 0.0)]
    yield "self-intersection", encoded(polygon_records(bow))
    triangle = [(0.0, 0.0), (4.0, 0.0), (0.0, 4.0)]
    unsupported = [
        record(circle((2.0, 0.0), 2.0)),
        record(line(triangle[1], triangle[2])),
        record(line(triangle[2], triangle[0])),
    ]
    yield "unsupported-circle", encoded(unsupported)
    degenerate = [
        record(line((0.0, 0.0), (0.0, 0.0))),
        record(line((0.0, 0.0), (1.0, 0.0))),
        record(line((1.0, 0.0), (0.0, 0.0))),
    ]
    yield "zero-length", encoded(degenerate)
    nonfinite = list(degenerate)
    nonfinite[0] = record(line((0.0, 0.0), (math.inf, 0.0)))
    yield "infinite-length", encoded(nonfinite)
    nan_length = list(degenerate)
    nan_length[0] = record(line((0.0, 0.0), (math.nan, 0.0)))
    yield "nan-length", encoded(nan_length)

    arc_pairs = [
        (0.0, math.pi / 2.0),
        (math.pi / 2.0, 0.0),
        (-2.4, 2.2),
        (2.7, -2.8),
        (7.0, 8.2),
    ]
    for pair_index, (start, end) in enumerate(arc_pairs):
        for scale_index, scale in enumerate((1e-3, 1.0, 1e4)):
            centre = (123456.0 if scale_index == 2 else -3.0, 4512345.0 if scale_index == 2 else 2.0)
            radius = 4.0 * scale
            first = (centre[0] + radius * math.cos(start), centre[1] + radius * math.sin(start))
            last = (centre[0] + radius * math.cos(end), centre[1] + radius * math.sin(end))
            middle = ((first[0] + last[0]) / 2.0, (first[1] + last[1]) / 2.0)
            curves = [
                record(arc(centre, radius, start, end), 0),
                record(line(last, middle), 1, 2),
                record(line(middle, first), 1),
            ]
            distance = radius * 0.02
            yield f"arc-{pair_index}-{scale_index}", encoded(
                curves, base=distance, other=distance * 0.7,
                tolerance=max(1e-12, scale * 1e-9),
            )

    rng = random.Random(953546713)
    for index in range(96):
        count = 3 + index % 8
        scale = 10.0 ** rng.uniform(-4.0, 5.0)
        offset_scale = 1e6 if index % 11 == 0 else 50.0
        offset = (rng.uniform(-offset_scale, offset_scale), rng.uniform(-offset_scale, offset_scale))
        angle = rng.uniform(-math.pi, math.pi)
        angles = sorted(rng.uniform(0.0, math.tau) for _ in range(count))
        radial = [scale * rng.uniform(3.0, 5.0) for _ in range(count)]
        points = [(radial[i] * math.cos(angles[i]), radial[i] * math.sin(angles[i])) for i in range(count)]
        if index % 2:
            points.reverse()
        points = transformed(points, angle, 1.0, offset)
        node = index % count
        faces = [0] * count
        faces[node] = 1
        previous = (node + count - 1) % count
        minimum = min(
            math.dist(points[previous], points[node]),
            math.dist(points[node], points[(node + 1) % count]),
        )
        fraction = rng.uniform(0.005, 0.08)
        selections = {} if index % 7 == 0 else {node: index % 12}
        yield f"convex-{index}", encoded(
            polygon_records(points, faces=faces, selections=selections),
            base=minimum * fraction,
            other=minimum * fraction * rng.uniform(0.3, 1.7),
            tolerance=max(scale * 1e-10, 1e-13),
            avoid_axis=index % 9 == 0,
        )

    for index in range(32):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        angle = rng.uniform(-math.pi, math.pi)
        offset = (rng.uniform(-1e5, 1e5), rng.uniform(-1e5, 1e5))
        points = transformed(bow, angle, scale, offset)
        yield f"crossing-{index}", encoded(
            polygon_records(points), tolerance=max(scale * 1e-10, 1e-12),
        )


def arguments(values):
    result = []
    for value in values:
        if isinstance(value, float):
            result.append(format(value, ".17g"))
        else:
            result.append(str(value))
    return result


def parsed(output):
    lines = output.splitlines()
    if not lines or lines[0] not in ("0", "1"):
        raise AssertionError(f"invalid output: {output!r}")
    cursor = 1
    if lines[0] == "0":
        if len(lines) != 3:
            raise AssertionError(f"invalid error output: {output!r}")
        return {"valid": False, "error": int(lines[1]), "edge": int(lines[2])}
    count = int(lines[cursor])
    cursor += 1
    curves = []
    for _ in range(count):
        kind = int(lines[cursor])
        cursor += 1
        field_count = 4 if kind == 0 else 5
        fields = tuple(float(value) for value in lines[cursor:cursor + field_count])
        cursor += field_count
        curves.append((kind, fields))
    if cursor != len(lines):
        raise AssertionError(f"trailing output: {output!r}")
    return {"valid": True, "curves": curves}


def same_float(actual, expected):
    if math.isnan(actual) or math.isnan(expected):
        return math.isnan(actual) and math.isnan(expected)
    if math.isinf(actual) or math.isinf(expected):
        return actual == expected
    return math.isclose(actual, expected, rel_tol=5e-12, abs_tol=5e-10)


def compare(name, actual, expected, mode):
    comparisons = 1
    if actual["valid"] != expected["valid"]:
        raise AssertionError(f"{name}/{mode}: validity mismatch: native={actual}, Rust={expected}")
    if not expected["valid"]:
        comparisons += 2
        if (actual["error"], actual["edge"]) != (expected["error"], expected["edge"]):
            raise AssertionError(f"{name}/{mode}: error mismatch: native={actual}, Rust={expected}")
        return comparisons
    comparisons += 1
    if len(actual["curves"]) != len(expected["curves"]):
        raise AssertionError(f"{name}/{mode}: output length mismatch")
    for curve_index, (native_curve, rust_curve) in enumerate(zip(actual["curves"], expected["curves"])):
        comparisons += 1
        if native_curve[0] != rust_curve[0] or len(native_curve[1]) != len(rust_curve[1]):
            raise AssertionError(f"{name}/{mode}: curve {curve_index} kind mismatch")
        for field_index, (native_value, rust_value) in enumerate(zip(native_curve[1], rust_curve[1])):
            comparisons += 1
            if not same_float(native_value, rust_value):
                raise AssertionError(
                    f"{name}/{mode}: curve {curve_index} field {field_index}: "
                    f"native={native_value!r}, Rust={rust_value!r}"
                )
    return comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument(
        "--rustc", type=Path,
        default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe",
    )
    parser.add_argument(
        "--dependencies", type=Path,
        default=ROOT / "build/topology-reference-deps/target/debug/deps",
    )
    parser.add_argument("--libraries", type=Path, default=ROOT / "build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
        "rev-parse", "HEAD",
    ])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
        "diff", "--exit-code", PIN, "--", "src/brep/chamfer_profile.rs",
    ])
    source_file = source / "src/brep/chamfer_profile.rs"
    if "#[test]" in source_file.read_text(encoding="utf-8"):
        raise AssertionError("unexpected source tests in chamfer_profile.rs")

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-chamfer-profile-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    reference_source = output / "reference.rs"
    copied_source = output / "chamfer_profile_source.rs"
    shutil.copy2(HERE / "reference.rs", reference_source)
    shutil.copy2(source_file, copied_source)

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
                args.rustc, "--edition=2021", reference_source,
                "--extern", f"cadkernel={library.resolve()}",
                "-L", f"dependency={args.dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected Rust diagnostics: {diagnostics}")
            detail = fixtures.verify_fixture(
                ROOT / "tests/required/cadkernel_brep_chamfer_profile",
                mode, args.compiler.resolve(), output, 120, 30, False,
            )
            diagnostics, native_seconds = process([
                args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
            binaries[mode] = {"rust": reference, "native": native}
            compile_times[mode] = {"rust": rust_seconds, "native": native_seconds}
            print(f"{mode}: {detail}; probe={native_seconds:.3f}s", flush=True)
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

    count = valid_cases = comparisons = 0
    exact_rust_optimization_parity = True
    for name, values in cases():
        if args.case not in name:
            continue
        command_arguments = arguments(values)
        outputs = {}
        raw = {}
        for mode in ("O0", "O2"):
            for implementation in ("rust", "native"):
                text, _ = process([binaries[mode][implementation], *command_arguments], timeout=30)
                raw[(mode, implementation)] = text
                outputs[(mode, implementation)] = parsed(text)
        if raw[("O0", "native")] != raw[("O2", "native")]:
            raise AssertionError(f"{name}: native optimization mismatch")
        if raw[("O0", "rust")] != raw[("O2", "rust")]:
            exact_rust_optimization_parity = False
        for mode in ("O0", "O2"):
            comparisons += compare(name, outputs[(mode, "native")], outputs[(mode, "rust")], mode)
        expected_o0 = outputs[("O0", "rust")]
        expected_o2 = outputs[("O2", "rust")]
        comparisons += compare(name, expected_o0, expected_o2, "Rust O0/O2")
        if expected_o0["valid"]:
            valid_cases += 1
        count += 1
        if count % 40 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 0,
        "native_groups": len((HERE / "expected.txt").read_text(encoding="utf-8").splitlines()),
        "cases": count,
        "valid_cases": valid_cases,
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
        "source_sha256": hashlib.sha256(source_file.read_bytes()).hexdigest(),
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_chamfer_profile.dl", HERE / "main.dl", HERE / "probe.dl")
        },
    }
    summary_name = "filtered-summary.json" if args.case else "summary.json"
    (output / summary_name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
