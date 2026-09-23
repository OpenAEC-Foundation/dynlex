# SPDX-License-Identifier: MPL-2.0
"""Compare planar face offsets with the pinned Rust implementation."""
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

HERE = ROOT / "tests/cadkernel/brep_presspull"
PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"


def process(command, *, timeout=300):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def encoded(kind=0, parameters=(4.0, 3.0, 2.0, 0.0), origin=(0.0, 0.0, 0.0), face=0, distance=0.25, mode=1):
    return [kind, *parameters, *origin, face, distance, mode]


def cases():
    for value in (-math.inf, -0.0, 0.0, 5e-324, math.inf, math.nan):
        yield f"invalid-distance-{value!r}", encoded(distance=value)
    for face in range(6):
        for distance in (0.25, -0.25):
            yield f"extrude-cuboid-face-{face}-distance-{distance}", encoded(
                face=face, distance=distance, mode=0,
            )
    families = (
        (0, (4.0, 3.0, 2.0, 0.0), 6),
        (1, (4.0, 3.0, 2.0, 0.0), 5),
        (2, (3.0, 4.0, 5.0, 0.0), 6),
        (3, (3.0, 1.5, 4.0, 5.0), 7),
        (4, (2.0, 5.0, 0.0, 0.0), 3),
    )
    for kind, parameters, faces in families:
        for face in range(faces):
            for distance in (0.05, -0.05, 0.25, -0.25, 5.0, -5.0):
                yield f"family-{kind}-face-{face}-distance-{distance}", encoded(
                    kind=kind, parameters=parameters, face=face, distance=distance,
                )

    for width, height in ((3.0, 2.0), (1e-3, 4e-3), (1e5, 2e5)):
        yield f"region-rectangle-{width}-{height}", encoded(kind=10, parameters=(width, height, 0.0, 0.0))
    for radius in (1e-3, 2.0, 1e5):
        yield f"region-circle-{radius}", encoded(kind=11, parameters=(radius, 0.0, 0.0, 0.0))
    for major, minor in ((3.0, 1.0), (1e-3, 4e-4), (1e5, 2e4)):
        yield f"region-ellipse-{major}-{minor}", encoded(kind=12, parameters=(major, minor, 0.0, 0.0))
    for width, height, margin in ((6.0, 5.0, 1.0), (1e-2, 8e-3, 1e-3), (1e5, 8e4, 1e4)):
        yield f"region-hole-{width}-{height}-{margin}", encoded(kind=13, parameters=(width, height, margin, 0.0))

    rng = random.Random(953_546_82)
    for index in range(80):
        kind = index % 5
        scale = 10.0 ** rng.uniform(-4.0, 5.0)
        world = 1e9 if index % 7 == 0 else 100.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        if kind == 0:
            parameters = tuple(scale * rng.uniform(2.0, 7.0) for _ in range(3)) + (0.0,)
            faces = 6
        elif kind == 1:
            parameters = (scale * 4.0, scale * 3.0, scale * 2.0, 0.0)
            faces = 5
        elif kind == 2:
            sides = rng.choice((3, 4, 5, 8, 17))
            parameters = (scale * 3.0, scale * 4.0, float(sides), 0.0)
            faces = sides + 1
        elif kind == 3:
            sides = rng.choice((3, 4, 5, 8, 17))
            parameters = (scale * 3.0, scale * 1.5, scale * 4.0, float(sides))
            faces = sides + 2
        else:
            parameters = (scale * 2.0, scale * 5.0, 0.0, 0.0)
            faces = 3
        distance = scale * rng.choice((-1.0, 1.0)) * rng.uniform(0.01, 0.2)
        yield f"random-{index}", encoded(
            kind=kind, parameters=parameters, origin=origin,
            face=rng.randrange(faces), distance=distance,
        )
    for index in range(20):
        scale = 10.0 ** rng.uniform(-4.0, 5.0)
        world = 1e9 if index % 5 == 0 else 100.0
        origin = tuple(rng.uniform(-world, world) for _ in range(3))
        kind = 10 + index % 4
        if kind == 10:
            parameters = (scale * 3.0, scale * 2.0, 0.0, 0.0)
        elif kind == 11:
            parameters = (scale * 2.0, 0.0, 0.0, 0.0)
        elif kind == 12:
            parameters = (scale * 3.0, scale, 0.0, 0.0)
        else:
            parameters = (scale * 6.0, scale * 5.0, scale, 0.0)
        yield f"random-region-{index}", encoded(kind=kind, parameters=parameters, origin=origin)


def arguments(values):
    return [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]


def parsed(output):
    result = []
    for line in output.splitlines():
        if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", line, re.IGNORECASE):
            result.append(math.nan)
        else:
            result.append(float(line))
    if not result or result[0] not in (0.0, 1.0):
        raise AssertionError(f"invalid probe output: {output!r}")
    expected = 1 if result[0] == 0.0 else 19 + int(result[18]) * 3
    if len(result) != expected:
        raise AssertionError(f"invalid probe field count {len(result)} != {expected}: {output!r}")
    return result


def same_float(left, right):
    if math.isnan(left) or math.isnan(right):
        return math.isnan(left) and math.isnan(right)
    if left == right:
        return True
    scale = max(1.0, abs(left), abs(right))
    return abs(left - right) <= 5e-9 + math.ulp(scale) * 96.0


def compare(name, mode, actual, expected):
    if actual[0] != expected[0]:
        raise AssertionError(f"{name}/{mode}: native valid={actual[0]} Rust valid={expected[0]}")
    if expected[0] == 0.0:
        return 1
    for field, (native, rust) in enumerate(zip(actual[:19], expected[:19])):
        if field == 6:
            coordinate_scale = max(
                [1.0, *(abs(value) for value in actual[19:]), *(abs(value) for value in expected[19:])]
            )
            if abs(native - rust) <= 5e-9 + math.ulp(coordinate_scale) * 96.0:
                continue
        if not same_float(native, rust):
            raise AssertionError(f"{name}/{mode} field {field}: native={native!r}, Rust={rust!r}")
    unmatched = [tuple(expected[index:index + 3]) for index in range(19, len(expected), 3)]
    for point in (tuple(actual[index:index + 3]) for index in range(19, len(actual), 3)):
        match = next((
            index for index, candidate in enumerate(unmatched)
            if all(same_float(left, right) for left, right in zip(point, candidate))
        ), None)
        if match is None:
            raise AssertionError(f"{name}/{mode}: native point {point!r} has no Rust match; remaining={unmatched!r}")
        unmatched.pop(match)
    if unmatched:
        raise AssertionError(f"{name}/{mode}: unmatched Rust points {unmatched!r}")
    return len(actual)


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
    checked_sources = [source / "src/brep/presspull.rs", source / "src/brep/make.rs"]
    process(["git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in checked_sources)])

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-presspull-", dir=ROOT / "build"))
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
            comparisons += compare(name, mode, outputs[(mode, "native")], outputs[(mode, "rust")])
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
        "compile_seconds": compile_times,
        "source_sha256": {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest() for path in checked_sources},
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_presspull.dl", HERE / "probe.dl", HERE / "main.dl")
        },
    }
    name = "filtered-summary.json" if args.case else "summary.json"
    (output / name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
