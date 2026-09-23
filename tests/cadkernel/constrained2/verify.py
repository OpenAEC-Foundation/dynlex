# SPDX-License-Identifier: MPL-2.0
"""Differential verification for the constrained parameter-domain mesh."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
REVISION = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
from verify import run_process, verify_fixture


def run(command, timeout=45):
    status, text, elapsed = run_process(list(map(str, command)), timeout=timeout,
                                        cwd=ROOT, phase="verification")
    if status:
        raise AssertionError(f"{command}: exit {status}\n{text}")
    return text, elapsed


def compile_program(command, timeout=120):
    text, _ = run(command, timeout=timeout)
    if text.strip():
        raise AssertionError(f"unexpected compiler output for {command}:\n{text}")


def source_is_pinned(source):
    path = "src/geom2d/constrained.rs"
    current = (source / path).read_text(encoding="utf-8")
    expected, _ = run(["git", "-c", f"safe.directory={source.as_posix()}",
                       "-C", source, "show", f"{REVISION}:{path}"])
    assert current == expected, f"{path} differs from {REVISION}"
    assert "#[test]" not in current, "constrained.rs unexpectedly acquired direct source tests"
    return current, path


def rectangle(x=0.0, y=0.0, width=10.0, height=None):
    height = width if height is None else height
    return [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]


def cases():
    result = []

    def add(name, rings, operations=()):
        values = [len(rings)]
        for ring in rings:
            values.extend([len(ring), *(coordinate for point in ring for coordinate in point)])
        values.append(len(operations))
        for operation in operations:
            values.extend(operation)
        result.append((name, values))

    square = rectangle()
    add("empty", [])
    add("short-ring", [[(0, 0), (1, 0)]])
    add("flat-x", [[(1, 0), (1, 1), (1, 2)]])
    add("flat-y", [[(0, 1), (1, 1), (2, 1)]])
    add("consecutive-duplicate", [[(0, 0), (10, 0), (10, 0), (0, 10)]])
    add("nonconsecutive-duplicate", [[(0, 0), (10, 0), (10, 10), (0, 0), (0, 10)]])
    add("bowtie", [[(0, 0), (10, 10), (10, 0), (0, 10)]])
    add("overlapping-rings", [square, rectangle(5, 5, 10)])
    add("touching-rings", [square, rectangle(10, 2, 3)])
    add("duplicate-rings", [square, square])
    add("nan", [[(0, 0), (10, 0), (math.nan, 10), (0, 10)]])
    add("infinite", [[(0, 0), (10, 0), (math.inf, 10), (0, 10)]])
    add("tiny", [rectangle(0, 0, 1e-200, 2e-200)])
    add("square", [square])
    add("closed-square", [square + square[:1]])
    add("reversed-square", [list(reversed(square))])
    add("hole", [square, rectangle(3, 3, 4)])
    add("two-components", [square, rectangle(20, 20, 2)])
    add("nested-island", [square, rectangle(2, 2, 6), rectangle(4, 4, 2)])
    add("insert-centre", [square], [(0, 5, 5), (0, 5, 5)])
    add("insert-boundary", [square], [(0, 5, 0), (0, 10, 5)])
    add("insert-outside", [square], [(0, 20, 20)])
    add("insert-nan", [square], [(0, math.nan, 5)])
    add("insert-infinite", [square], [(0, math.inf, 5)])
    add("insert-hole", [square, rectangle(3, 3, 4)], [(0, 5, 5), (0, 2, 2)])
    add("horizontal-constraint", [square], [(1, 0, 5, 10, 5)])
    add("crossing-constraints", [square], [(1, 0, 5, 10, 5), (1, 5, 0, 5, 10)])
    add("diagonal-constraint", [square], [(1, 0, 0, 10, 10)])
    add("same-point-constraint", [square], [(1, 4, 4, 4, 4)])
    add("boundary-constraint", [square], [(1, 0, 0, 10, 0)])
    add("partial-boundary-constraint", [square], [(1, 2, 0, 8, 0)])
    add("outside-constraint", [square], [(1, -5, 5, 15, 5)])
    add("duplicate-constraint", [square], [(1, 0, 5, 10, 5), (1, 0, 5, 10, 5)])
    add("constraint-nan", [square], [(1, math.nan, 0, 10, 10)])
    add("constraint-with-free-vertex", [square], [(0, 5, 5), (1, 0, 5, 10, 5)])
    survey = rectangle(512345.678, 4512345.678, 10, 8)
    add("survey", [survey], [(0, 512350.678, 4512349.678),
                              (1, 512345.678, 4512349.678, 512355.678, 4512349.678)])
    rng = random.Random(953546)
    for index in range(32):
        x, y = rng.uniform(-100, 100), rng.uniform(-100, 100)
        width, height = rng.uniform(.5, 30), rng.uniform(.5, 30)
        ring = rectangle(x, y, width, height)
        cx, cy = x + width * rng.uniform(.1, .9), y + height * rng.uniform(.1, .9)
        operations = [(0, cx, cy)]
        if index % 2 == 0:
            operations.append((1, x, cy, x + width, cy))
        if index % 4 == 0:
            operations.append((1, cx, y, cx, y + height))
        add(f"random-{index}", [ring], operations)
    return result


def argument(value):
    if isinstance(value, float) and math.isnan(value):
        return "nan"
    return repr(value)


def parse(text):
    lines = [line.split() for line in text.splitlines() if line.strip()]
    assert lines and lines[0][0] == "b", text
    built = lines[0][1] == "1"
    operations = []
    triangles = []
    declared = None
    for fields in lines[1:]:
        if fields[0] in ("i", "c"):
            operations.append((fields[0], int(fields[1]), int(fields[2])))
        elif fields[0] == "n":
            declared = int(fields[1])
        elif fields[0] == "t":
            values = list(map(float, fields[1:7]))
            flags = tuple(map(int, fields[7:10]))
            triangles.append((tuple(zip(values[::2], values[1::2])), flags))
        else:
            raise AssertionError(fields)
    if built:
        assert declared == len(triangles), (declared, len(triangles), text)
    return built, operations, triangles


def point_key(point, digits=11):
    return tuple(round(value, digits) for value in point)


def semantics(triangles):
    area = 0.0
    vertices = set()
    constrained = Counter()
    for points, flags in triangles:
        a, b, c = points
        coordinate_scale = max(abs(value) for point in ((b[0] - a[0], b[1] - a[1]),
                                                        (c[0] - a[0], c[1] - a[1])) for value in point)
        assert math.isfinite(coordinate_scale) and coordinate_scale > 0.0, (points, flags)
        bx, by = (b[0] - a[0]) / coordinate_scale, (b[1] - a[1]) / coordinate_scale
        cx, cy = (c[0] - a[0]) / coordinate_scale, (c[1] - a[1]) / coordinate_scale
        normalized_twice = bx * cy - by * cx
        assert math.isfinite(normalized_twice) and abs(normalized_twice) > 0.0, (points, flags)
        twice = normalized_twice * coordinate_scale * coordinate_scale
        area += abs(twice) * 0.5
        vertices.update(map(point_key, points))
        for (left, right), flag in zip(((a, b), (b, c), (c, a)), flags):
            if flag:
                edge = tuple(sorted((point_key(left), point_key(right))))
                constrained[edge] += 1
    return dict(count=len(triangles), area=area, vertices=sorted(vertices),
                constrained=sorted(constrained.items()))


def compare(actual, expected, name, mode):
    assert actual[0] == expected[0], f"{name}/{mode}: constructor {actual[0]} != {expected[0]}"
    if not expected[0]:
        return 1
    assert actual[1] == expected[1], f"{name}/{mode}: operations {actual[1]} != {expected[1]}"
    got, want = semantics(actual[2]), semantics(expected[2])
    scale = max(1.0, abs(want["area"]))
    assert math.isclose(got["area"], want["area"], rel_tol=1e-10, abs_tol=1e-9 * scale), \
        f"{name}/{mode}: area {got['area']} != {want['area']}"
    for field in ("count", "vertices", "constrained"):
        assert got[field] == want[field], f"{name}/{mode}: {field}\n{got[field]}\n!=\n{want[field]}"
    return 3 + len(want["vertices"]) + len(want["constrained"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe")
    parser.add_argument("--dependencies", type=Path, default=ROOT / "build/topology-reference-deps/target/debug/deps")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    constrained_source, source_path = source_is_pinned(source)
    output = args.output or Path(tempfile.mkdtemp(prefix="constrained2-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    spade = list(args.dependencies.resolve().glob("libspade-*.rlib"))
    assert len(spade) == 1, spade
    driver = output / "reference-driver.rs"
    driver.write_text("mod constrained {\n" + constrained_source + "\n" +
                      (HERE / "reference.rs").read_text(encoding="utf-8") +
                      "\n}\nfn main() { constrained::run_probe(); }\n", encoding="utf-8")
    if not args.skip_build:
        for mode in ("O0", "O2"):
            compile_program([args.rustc, "--edition=2021", driver, "--extern", f"spade={spade[0]}",
                             "-L", f"dependency={args.dependencies.resolve()}", "-A", "dead_code",
                             "-C", f"opt-level={mode[1]}", "-o", output / f"reference-{mode}.out"])
            compile_program([args.compiler, HERE / "probe.dl", f"-{mode}", "-o", output / f"probe-{mode}.out"], timeout=120)
            for fixture in ("cadkernel_constrained2", "cadkernel_constrained2_ownership"):
                print(f"{fixture}/{mode}: " + verify_fixture(ROOT / "tests/required" / fixture, mode,
                      args.compiler, output, 180, 30, False), flush=True)
    selected = [(name, values) for name, values in cases() if args.case in name]
    comparisons = 0
    for index, (name, values) in enumerate(selected):
        arguments = list(map(argument, values))
        expected_text, _ = run([output / "reference-O0.out", *arguments])
        optimized_text, _ = run([output / "reference-O2.out", *arguments])
        expected, optimized = parse(expected_text), parse(optimized_text)
        compare(optimized, expected, name, "Rust-O2")
        for mode in ("O0", "O2"):
            actual_text, _ = run([output / f"probe-{mode}.out", *arguments])
            comparisons += compare(parse(actual_text), expected, name, mode)
        if (index + 1) % 10 == 0:
            print(f"{index + 1}/{len(selected)} constrained cases passed", flush=True)
    assert selected and hashlib.sha256(args.compiler.read_bytes()).hexdigest() == digest
    report = dict(revision=REVISION, compiler_sha256=digest, source_tests=0,
                  cases=len(selected), comparisons=comparisons, case_filter=args.case,
                  semantic_triangle_comparison=True)
    report["source_sha256"] = {source_path: hashlib.sha256(constrained_source.encode()).hexdigest()}
    report["native_source_sha256"] = {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (ROOT / "lib/cadkernel/constrained2.dl", HERE / "probe.dl",
                     HERE / "reference.rs", ROOT / "tests/required/cadkernel_constrained2/main.dl",
                     ROOT / "tests/required/cadkernel_constrained2_ownership/main.dl")
    }
    (output / ("filtered-summary.json" if args.case else "summary.json")).write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(selected)} cases, {comparisons} semantic comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
