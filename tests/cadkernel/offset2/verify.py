#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native polyline offsets with the pinned all-features implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/offset2"


def process(command, *, timeout=180):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(vertices, closed, distance, side):
    return [int(closed), distance, *side, len(vertices), *(value for vertex in vertices for value in vertex)]


def rectangle(origin, width, height):
    x, y = origin
    return [(x, y, 0.0), (x + width, y, 0.0),
            (x + width, y + height, 0.0), (x, y + height, 0.0)]


def cases():
    box = rectangle((0.0, 0.0), 10.0, 6.0)
    yield "rectangle-out", encoded(box, True, 1.0, (-5.0, 3.0))
    yield "rectangle-in", encoded(box, True, 1.0, (5.0, 3.0))
    yield "rectangle-collapse", encoded(box, True, 5.0, (5.0, 3.0))
    yield "zero-distance", encoded(box, True, 0.0, (5.0, 3.0))
    yield "lone", encoded([(0.0, 0.0, 0.0)], False, 1.0, (1.0, 1.0))
    yield "open-elbow", encoded(
        [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (10.0, 6.0, 0.0)],
        False, 1.0, (5.0, 1.0),
    )
    survey = rectangle((512345.678, 4512345.678), 10.0, 6.0)
    yield "survey", encoded(survey, True, 1.0, (512350.678, 4512348.678))
    yield "semicircle-in", encoded([(0.0, 0.0, 1.0), (4.0, 0.0, 0.0)], True, 0.5, (2.0, 0.1))
    yield "semicircle-out", encoded([(0.0, 0.0, 1.0), (4.0, 0.0, 0.0)], True, 0.5, (2.0, 3.0))
    yield "circle-in", encoded([(4.0, 0.0, 1.0), (-4.0, 0.0, 1.0)], True, 0.75, (0.0, 0.0))
    yield "circle-out", encoded([(4.0, 0.0, 1.0), (-4.0, 0.0, 1.0)], True, 0.75, (8.0, 0.0))
    major = math.tan(3.0 * math.pi / 8.0)
    yield "major-arc", encoded([(0.0, 0.0, major), (4.0, 0.0, 0.0)], True, 0.25, (2.0, 0.1))

    concave = [(0.0, 0.0, 0.0), (8.0, 0.0, 0.0), (8.0, 2.0, 0.0),
               (3.0, 2.0, 0.0), (3.0, 7.0, 0.0), (0.0, 7.0, 0.0)]
    for distance in (0.1, 0.5, 1.0, 2.0):
        yield f"concave-in-{distance}", encoded(concave, True, distance, (1.0, 1.0))
        yield f"concave-out-{distance}", encoded(concave, True, distance, (-2.0, 3.0))

    rng = random.Random(0x0FF5E7)
    for index in range(64):
        origin = (rng.uniform(-1e4, 1e4), rng.uniform(-1e4, 1e4))
        width = 10.0 ** rng.uniform(-2.0, 3.0)
        height = 10.0 ** rng.uniform(-2.0, 3.0)
        distance = min(width, height) * rng.uniform(0.01, 0.45)
        inward = index % 2 == 0
        side = (origin[0] + width * 0.5, origin[1] + height * 0.5) if inward else (
            origin[0] - width, origin[1] + height * 0.5)
        yield f"random-rectangle-{index}", encoded(
            rectangle(origin, width, height), True, distance, side,
        )
    for index in range(32):
        x, y = rng.uniform(-100.0, 100.0), rng.uniform(-100.0, 100.0)
        first = rng.uniform(0.5, 20.0)
        second = rng.uniform(0.5, 20.0)
        distance = min(first, second) * rng.uniform(0.01, 0.4)
        vertices = [(x, y, 0.0), (x + first, y, 0.0), (x + first, y + second, 0.0)]
        yield f"random-open-{index}", encoded(vertices, False, distance, (x + first * 0.5, y + distance))
    for index in range(24):
        radius = rng.uniform(0.5, 50.0)
        distance = radius * rng.uniform(0.01, 0.4)
        bulge = 1.0 if index % 2 == 0 else -1.0
        side_y = radius * 0.1 * (1.0 if bulge > 0.0 else -1.0)
        yield f"random-semicircle-{index}", encoded(
            [(0.0, 0.0, bulge), (2.0 * radius, 0.0, 0.0)],
            True, distance, (radius, side_y),
        )


def parsed(text):
    values = [float(value) for value in text.replace("\r", "").splitlines() if value.strip()]
    cursor = 0
    count = int(values[cursor]); cursor += 1
    result = []
    for _ in range(count):
        closed = bool(int(values[cursor])); cursor += 1
        vertex_count = int(values[cursor]); cursor += 1
        vertices = []
        for _ in range(vertex_count):
            vertices.append(tuple(values[cursor:cursor + 3])); cursor += 3
        result.append((closed, vertices))
    if cursor != len(values):
        raise AssertionError(f"unparsed offset output at field {cursor}/{len(values)}")
    return result


def canonical(polylines):
    normalized = []
    for closed, vertices in polylines:
        if closed and vertices:
            keys = [tuple(round(value, 10) for vertex in vertices[index:] + vertices[:index] for value in vertex)
                    for index in range(len(vertices))]
            start = min(range(len(keys)), key=keys.__getitem__)
            vertices = vertices[start:] + vertices[:start]
        normalized.append((closed, vertices))
    return sorted(normalized, key=lambda item: (
        not item[0], len(item[1]),
        tuple(round(value, 10) for vertex in item[1] for value in vertex),
    ))


def compare(name, mode, actual, expected):
    actual, expected = canonical(actual), canonical(expected)
    if len(actual) != len(expected):
        raise AssertionError(f"{name}/{mode}: {len(actual)} native loops != {len(expected)} Rust loops")
    comparisons = 1
    for loop_index, ((actual_closed, actual_vertices), (expected_closed, expected_vertices)) in enumerate(zip(actual, expected)):
        if actual_closed != expected_closed or len(actual_vertices) != len(expected_vertices):
            raise AssertionError(
                f"{name}/{mode} loop {loop_index}: native closed/count "
                f"{actual_closed}/{len(actual_vertices)} != Rust {expected_closed}/{len(expected_vertices)}"
            )
        comparisons += 2
        for vertex_index, (actual_vertex, expected_vertex) in enumerate(zip(actual_vertices, expected_vertices)):
            for field, (got, want) in enumerate(zip(actual_vertex, expected_vertex)):
                tolerance = max(2e-8, abs(want) * 2e-12)
                if not math.isclose(got, want, rel_tol=0.0, abs_tol=tolerance):
                    raise AssertionError(
                        f"{name}/{mode} loop {loop_index} vertex {vertex_index} field {field}: "
                        f"native={got!r}, Rust={want!r}, tolerance={tolerance!r}"
                    )
                comparisons += 1
    return comparisons


def one_library(directory):
    libraries = list(directory.glob("libcadkernel-*.rlib"))
    if len(libraries) != 1:
        raise AssertionError(f"expected one cadkernel rlib in {directory}, found {libraries}")
    return libraries[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path.home() / ".cargo/bin/rustc.exe")
    parser.add_argument("--reference-target", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_file = source / "src/geom2d/offset.rs"
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
             "diff", "--exit-code", PIN, "--", source_file.relative_to(source)])

    output = args.output or Path(tempfile.mkdtemp(prefix="offset2-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    binaries = {}
    compile_times = {}
    for mode, profile in (("O0", "debug"), ("O2", "release")):
        deps = args.reference_target / profile / "deps"
        library = one_library(deps)
        reference = output / f"reference-{mode}{suffix}"
        native = output / f"native-{mode}{suffix}"
        _, rust_seconds = process([
            args.rustc, "--edition=2021", HERE / "reference.rs",
            "--extern", f"cadkernel={library.resolve()}",
            "-L", f"dependency={deps.resolve()}",
            "-C", f"opt-level={mode[1]}", "-o", reference,
        ])
        diagnostics, native_seconds = process([
            args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
        ])
        if diagnostics:
            raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
        binaries[mode] = (reference, native)
        compile_times[mode] = {"rust": rust_seconds, "native": native_seconds}
        print(f"{mode}: Rust={rust_seconds:.3f}s native={native_seconds:.3f}s", flush=True)

    count = comparisons = valid = 0
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        native_by_mode = {}
        for mode in ("O0", "O2"):
            reference_text, _ = process([binaries[mode][0], *arguments], timeout=30)
            native_text, _ = process([binaries[mode][1], *arguments], timeout=30)
            expected = parsed(reference_text)
            actual = parsed(native_text)
            comparisons += compare(name, mode, actual, expected)
            valid += int(bool(expected))
            native_by_mode[mode] = canonical(actual)
        compare(name, "native-O0/O2", native_by_mode["O0"], native_by_mode["O2"])
        count += 1
        if count % 25 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 10,
        "native_groups": 10,
        "cases": count,
        "valid_mode_results": valid,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_structure": True,
        "compile_seconds": compile_times,
        "source_sha256": hashlib.sha256(source_file.read_bytes()).hexdigest(),
        "native_source_sha256": hashlib.sha256((ROOT / "lib/cadkernel/offset2.dl").read_bytes()).hexdigest(),
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
