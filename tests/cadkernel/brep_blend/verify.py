#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare public planar chamfers with the pinned Rust implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

HERE = ROOT / "tests/cadkernel/brep_blend"
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"


def process(command, *, timeout=240):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(
    kind=0,
    parameters=(4.0, 3.0, 2.0, 0.0),
    origin=(0.0, 0.0, 0.0),
    selected=(0,),
    face=0,
    base=0.2,
    other=0.15,
    operation=0,
):
    chosen = list(selected[:3])
    chosen.extend([0] * (3 - len(chosen)))
    return [kind, *parameters, *origin, len(selected), *chosen, face, base, other, operation]


def cases():
    yield "empty-selection", encoded(selected=())
    for index, value in enumerate((-math.inf, -1.0, -0.0, 0.0, 5e-324, math.inf, math.nan)):
        yield f"invalid-base-{index}", encoded(base=value)
        yield f"invalid-other-{index}", encoded(other=value)

    for edge in range(9):
        for face in range(5):
            yield f"wedge-edge-{edge}-face-{face}", encoded(selected=(edge,), face=face)
    for edge in (0, 1, 2, 3, 6, 7, 8):
        yield f"wedge-asymmetric-{edge}", encoded(
            selected=(edge,), face=edge % 5, base=0.37, other=0.11,
        )
        yield f"wedge-oversized-{edge}", encoded(
            selected=(edge,), face=edge % 5, base=20.0, other=15.0,
        )

    for sides in (3, 4, 5, 8, 17):
        pyramid_edges = sorted({0, 1, sides - 1, sides, sides + 1, 2 * sides - 1})
        pyramid_faces = sorted({0, 1, sides - 1, sides})
        for edge in pyramid_edges:
            for face in pyramid_faces:
                yield f"pyramid-{sides}-edge-{edge}-face-{face}", encoded(
                    kind=1,
                    parameters=(3.0, 4.0, float(sides), 0.0),
                    selected=(edge,),
                    face=face,
                    base=0.23,
                    other=0.17,
                )

        frustum_edges = sorted({
            0, 1, sides - 1, sides, sides + 1,
            2 * sides - 1, 2 * sides, 3 * sides - 1,
        })
        frustum_faces = sorted({0, 1, sides, sides + 1})
        for edge in frustum_edges:
            for face in frustum_faces:
                yield f"frustum-{sides}-edge-{edge}-face-{face}", encoded(
                    kind=2,
                    parameters=(3.0, 1.25, 4.0, float(sides)),
                    selected=(edge,),
                    face=face,
                    base=0.19,
                    other=0.31,
                )

    for selection in ((0, 1), (0, 0), (0, 1, 2), (3, 4), (6, 7)):
        for face in range(5):
            yield f"wedge-multi-{selection}-{face}", encoded(selected=selection, face=face)

    rng = random.Random(953_546_71)
    for index in range(30):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        origin_scale = 1e8 if index % 6 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        if index % 3 == 0:
            parameters = tuple(scale * rng.uniform(2.0, 7.0) for _ in range(3)) + (0.0,)
            yield f"random-wedge-{index}", encoded(
                parameters=parameters,
                origin=origin,
                selected=(rng.randrange(9),),
                face=rng.randrange(5),
                base=scale * rng.uniform(0.01, 0.1),
                other=scale * rng.uniform(0.01, 0.1),
            )
        elif index % 3 == 1:
            sides = rng.choice((3, 4, 5, 8))
            yield f"random-pyramid-{index}", encoded(
                kind=1,
                parameters=(scale * 3.0, scale * 5.0, float(sides), 0.0),
                origin=origin,
                selected=(rng.randrange(2 * sides),),
                face=rng.randrange(sides + 1),
                base=scale * rng.uniform(0.01, 0.08),
                other=scale * rng.uniform(0.01, 0.08),
            )
        else:
            sides = rng.choice((3, 4, 5, 8))
            yield f"random-frustum-{index}", encoded(
                kind=2,
                parameters=(scale * 3.0, scale * 1.25, scale * 5.0, float(sides)),
                origin=origin,
                selected=(rng.randrange(3 * sides),),
                face=rng.randrange(sides + 2),
                base=scale * rng.uniform(0.01, 0.08),
                other=scale * rng.uniform(0.01, 0.08),
            )

    yield "fillet-empty-selection", encoded(selected=(), operation=1)
    for index, value in enumerate((-math.inf, -1.0, -0.0, 0.0, 5e-324, math.inf, math.nan)):
        yield f"fillet-invalid-radius-{index}", encoded(base=value, operation=1)
    for edge in range(9):
        for radius in (0.05, 0.2, 0.75, 4.0):
            yield f"fillet-wedge-edge-{edge}-radius-{radius}", encoded(
                selected=(edge,), base=radius, operation=1,
            )
    for sides in (3, 4, 5, 8, 17):
        pyramid_edges = sorted({0, 1, sides - 1, sides, sides + 1, 2 * sides - 1})
        for edge in pyramid_edges:
            yield f"fillet-pyramid-{sides}-edge-{edge}", encoded(
                kind=1,
                parameters=(3.0, 4.0, float(sides), 0.0),
                selected=(edge,),
                base=0.2,
                operation=1,
            )
        frustum_edges = sorted({
            0, 1, sides - 1, sides, sides + 1,
            2 * sides - 1, 2 * sides, 3 * sides - 1,
        })
        for edge in frustum_edges:
            yield f"fillet-frustum-{sides}-edge-{edge}", encoded(
                kind=2,
                parameters=(3.0, 1.25, 4.0, float(sides)),
                selected=(edge,),
                base=0.2,
                operation=1,
            )
    for selection in ((0, 1), (0, 0), (0, 1, 2), (6, 7)):
        yield f"fillet-wedge-multi-{selection}", encoded(
            selected=selection, base=0.2, operation=1,
        )

    for edge in range(7):
        for face in range(2):
            yield f"sheet-edge-{edge}-face-{face}", encoded(
                kind=3,
                parameters=(3.0, 2.0, 4.0, 0.0),
                selected=(edge,),
                face=face,
                base=0.2,
                other=0.15,
            )
    for index in range(18):
        scale = 10.0 ** rng.uniform(-4.0, 5.0)
        origin_scale = 1e9 if index % 5 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        yield f"sheet-random-{index}", encoded(
            kind=3,
            parameters=(scale * 3.0, scale * 2.0, scale * 4.0, 0.0),
            origin=origin,
            selected=(5,),
            face=index % 2,
            base=scale * rng.uniform(0.01, 0.12),
            other=scale * rng.uniform(0.01, 0.12),
        )

    for edge in range(15):
        yield f"existing-fillet-edge-{edge}", encoded(
            kind=4,
            parameters=(6.0, 5.0, 4.0, 0.0),
            selected=(edge,),
            base=0.2,
            other=0.3,
            operation=2,
        )
        for face in range(7):
            yield f"existing-fillet-chamfer-edge-{edge}-face-{face}", encoded(
                kind=4,
                parameters=(6.0, 5.0, 4.0, 0.0),
                selected=(edge,),
                face=face,
                base=0.2,
                other=0.3,
                operation=3,
            )
    for index in range(12):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        origin_scale = 1e9 if index % 4 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        yield f"existing-fillet-random-{index}", encoded(
            kind=4,
            parameters=(scale * 6.0, scale * 5.0, scale * 4.0, 0.0),
            origin=origin,
            selected=(2,),
            base=scale * 0.04,
            other=scale * 0.06,
            operation=2,
        )

    corner_pairs = (
        (2, 3), (2, 12), (2, 13), (3, 13), (3, 14),
        (7, 8), (7, 12), (7, 13), (8, 13), (8, 14),
        (0, 1), (0, 4), (2, 14), (3, 12), (12, 13), (13, 14),
        (2, 2), (3, 2),
    )
    for selection in corner_pairs:
        yield f"corner-pair-{selection}", encoded(
            kind=4,
            parameters=(6.0, 5.0, 4.0, 0.0),
            selected=selection,
            base=0.15,
            other=0.3,
            operation=4,
        )
    corner_triples = (
        (2, 3, 13), (7, 8, 13), (2, 3, 12), (2, 12, 13),
        (3, 13, 14), (2, 3, 14), (2, 3, 3),
    )
    for selection in corner_triples:
        yield f"corner-triple-{selection}", encoded(
            kind=4,
            parameters=(6.0, 5.0, 4.0, 0.0),
            selected=selection,
            base=0.15,
            other=0.3,
            operation=4,
        )
    for radius in (0.05, 0.15, 0.75, 10.0):
        for selection in ((2, 3), (2, 3, 13)):
            yield f"corner-radius-{radius}-{selection}", encoded(
                kind=4,
                parameters=(6.0, 5.0, 4.0, 0.0),
                selected=selection,
                base=radius,
                other=0.3,
                operation=4,
            )
    for index in range(12):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        origin_scale = 1e9 if index % 4 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        selection = (2, 3) if index % 2 == 0 else (2, 3, 13)
        yield f"corner-random-{index}", encoded(
            kind=4,
            parameters=(scale * 6.0, scale * 5.0, scale * 4.0, 0.0),
            origin=origin,
            selected=selection,
            base=scale * 0.15,
            other=scale * 0.3,
            operation=4,
        )

    for edge in (11, 12):
        yield f"existing-corner-fillet-edge-{edge}", encoded(
            kind=4,
            parameters=(6.0, 5.0, 4.0, 0.0),
            selected=(edge,),
            base=0.1,
            other=0.3,
            operation=5,
        )
    for edge in (12,):
        for face in (2, 3):
            yield f"existing-corner-chamfer-edge-{edge}-face-{face}", encoded(
                kind=4,
                parameters=(6.0, 5.0, 4.0, 0.0),
                selected=(edge,),
                face=face,
                base=0.1,
                other=0.3,
                operation=6,
            )
    for index in range(8):
        scale = 10.0 ** rng.uniform(-3.0, 4.0)
        origin_scale = 1e9 if index % 4 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        operation = 5 if index % 2 == 0 else 6
        edge = 11 if operation == 5 else 12
        yield f"existing-corner-random-{index}", encoded(
            kind=4,
            parameters=(scale * 6.0, scale * 5.0, scale * 4.0, 0.0),
            origin=origin,
            selected=(edge,),
            face=2,
            base=scale * 0.1,
            other=scale * 0.3,
            operation=operation,
        )


def arguments(values):
    return [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]


def parsed(output):
    result = []
    for line in output.splitlines():
        if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", line, re.IGNORECASE):
            result.append(math.nan)
        else:
            result.append(float(line))
    if len(result) < 3 or result[0] not in (0.0, 1.0):
        raise AssertionError(f"invalid probe output: {output!r}")
    return result


def same_float(actual, expected):
    if math.isnan(actual) or math.isnan(expected):
        return math.isnan(actual) and math.isnan(expected)
    if actual == expected:
        return True
    if math.isinf(actual) or math.isinf(expected):
        return False
    return math.isclose(actual, expected, rel_tol=5e-11, abs_tol=5e-9)


def compare(name, mode, actual, expected):
    if len(actual) != len(expected):
        raise AssertionError(
            f"{name}/{mode}: {len(actual)} native fields != {len(expected)} Rust fields; "
            f"native={actual!r}, Rust={expected!r}"
        )
    point_offset = 21
    scalar_limit = point_offset if expected[0] == 1.0 else len(expected)
    for field, (native, rust) in enumerate(zip(actual[:scalar_limit], expected[:scalar_limit])):
        if field == 2:
            keyed_errors = {1.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0}
            if expected[0] == 1.0 or expected[1] not in keyed_errors:
                continue
        if field == 8 and expected[0] == 1.0:
            coordinate_scale = max(
                [1.0, *(abs(value) for value in actual[point_offset:]), *(abs(value) for value in expected[point_offset:])]
            )
            gap_margin = 5e-9 + math.ulp(1.0) * coordinate_scale * 64.0
            if math.isfinite(native) and math.isfinite(rust) and abs(native - rust) <= gap_margin:
                continue
        if not same_float(native, rust):
            raise AssertionError(
                f"{name}/{mode} field {field}: native={native!r}, Rust={rust!r}"
            )
    if expected[0] == 1.0:
        native_points = [
            tuple(actual[index:index + 3]) for index in range(point_offset, len(actual), 3)
        ]
        rust_points = [
            tuple(expected[index:index + 3]) for index in range(point_offset, len(expected), 3)
        ]
        unmatched = list(rust_points)
        for point in native_points:
            match = next((
                index for index, candidate in enumerate(unmatched)
                if all(same_float(left, right) for left, right in zip(point, candidate))
            ), None)
            if match is None:
                raise AssertionError(
                    f"{name}/{mode}: native point {point!r} has no Rust match; "
                    f"remaining={unmatched!r}"
                )
            unmatched.pop(match)
        if unmatched:
            raise AssertionError(f"{name}/{mode}: unmatched Rust points {unmatched!r}")
    return len(actual)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument(
        "--rustc",
        type=Path,
        default=Path.home() / ".rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe",
    )
    parser.add_argument(
        "--dependencies",
        type=Path,
        default=ROOT / "build/topology-reference-deps/target/debug/deps",
    )
    parser.add_argument("--libraries", type=Path, default=ROOT / "build/brep-make-six-checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="")
    parser.add_argument(
        "--optimization", action="append", choices=("O0", "O2"),
        help="repeatable; defaults to both O0 and O2",
    )
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD",
    ])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    checked_sources = [source / "src/brep/blend.rs", source / "src/brep/make.rs"]
    process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
        "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in checked_sources),
    ])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-blend-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ".out"
    modes = list(dict.fromkeys(args.optimization or ("O0", "O2")))
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    binaries = {}
    compile_times = {}
    if not args.skip_build:
        for mode in modes:
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
            ], timeout=300)
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
            if not all(path.is_file() for path in binaries[mode].values()):
                raise AssertionError(f"missing --skip-build binaries for {mode}")
    if args.build_only:
        return

    count = valid = comparisons = 0
    for name, values in cases():
        if args.case not in name:
            continue
        command_args = arguments(values)
        outputs = {}
        for mode in modes:
            for implementation in ("rust", "native"):
                raw, _ = process([binaries[mode][implementation], *command_args], timeout=30)
                outputs[(mode, implementation)] = parsed(raw)
        for mode in modes:
            comparisons += compare(
                name, mode, outputs[(mode, "native")], outputs[(mode, "rust")],
            )
        valid += int(outputs[(modes[0], "rust")][0])
        count += 1
        if count % 50 == 0:
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
        "compile_seconds": compile_times,
        "reference_library_sha256": {
            mode: hashlib.sha256((args.libraries / f"libcadkernel-{mode}.rlib").read_bytes()).hexdigest()
            for mode in modes
        },
        "source_sha256": {
            str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in checked_sources
        },
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_blend.dl", HERE / "probe.dl", HERE / "main.dl")
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
