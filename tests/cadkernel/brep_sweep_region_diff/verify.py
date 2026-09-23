#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare pinned Rust and native DynLex lateral region sheets."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SWEEP_HASH = "492ac57683c6681f6e84f9fbbbdee2001b2b18a2bd766eb714588f90e4ffb995"
PRESSPULL_HASH = "273ab904d7b85b3e25b85d467b7dbd12397e1fe5ed2b229a47c7ca2a89f3c96d"


def process(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def line(start: tuple[float, float], end: tuple[float, float]) -> list[float]:
    return [0, *start, *end]


def circle(centre: tuple[float, float], radius: float) -> list[float]:
    return [1, *centre, radius]


def arc(centre: tuple[float, float], radius: float, start: float, end: float) -> list[float]:
    return [2, *centre, radius, start, end]


def polygon(points: list[tuple[float, float]]) -> list[list[float]]:
    return [line(points[index], points[(index + 1) % len(points)]) for index in range(len(points))]


def polyline(points: list[tuple[float, float]], *, closed: bool = True) -> list[float]:
    return [4, int(closed), len(points), *(value for point in points for value in (*point, 0.0))]


def encoded(mode: int, profiles: list[list[list[float]]], *,
            direction: tuple[float, float, float] = (0.0, 0.0, 2.0),
            angle: float = 0.1) -> list[float]:
    values: list[float] = [mode, *direction, angle, len(profiles)]
    for profile in profiles:
        values.extend([len(profile), *(value for piece in profile for value in piece)])
    return values


def cases():
    square = polygon([(0.0, 0.0), (8.0, 0.0), (8.0, 8.0), (0.0, 8.0)])
    inner_circle = [circle((4.0, 4.0), 1.0)]
    square_polyline = [polyline([(0.0, 0.0), (8.0, 0.0), (8.0, 8.0), (0.0, 8.0)])]
    two_circles = [[circle((0.0, 0.0), 5.0)], [circle((0.0, 0.0), 1.0)]]
    full_arc = [[arc((0.0, 0.0), 5.0, 0.25, 0.25 + math.tau)],
                [circle((0.0, 0.0), 1.0)]]
    quarter_arcs = [[arc((0.0, 0.0), 5.0, i * math.pi / 2, (i + 1) * math.pi / 2)
                     for i in range(4)], [circle((0.0, 0.0), 1.0)]]
    three_loops = [square, [circle((2.0, 2.0), 0.75)],
                   polygon([(5.0, 5.0), (7.0, 5.0), (7.0, 7.0), (5.0, 7.0)])]
    for name, profiles in (
        ("square-circle", [square, inner_circle]),
        ("polyline-circle", [square_polyline, inner_circle]),
        ("two-circles", two_circles),
        ("full-arc-circle", full_arc),
        ("quarter-arcs-circle", quarter_arcs),
        ("three-loops", three_loops),
    ):
        for mode in (0, 1):
            yield f"{name}-{'taper' if mode else 'plain'}", encoded(mode, profiles), 1
    for mode in (0, 1):
        label = "taper" if mode else "plain"
        yield f"empty-{label}", encoded(mode, []), 0
        yield f"empty-inner-{label}", encoded(mode, [square, []]), 0
        yield f"zero-direction-{label}", encoded(mode, [square], direction=(0.0, 0.0, 0.0)), 0
    small_circle = [[circle((0.0, 0.0), 0.5)]]
    yield "collapsed-cone-taper", encoded(1, small_circle, direction=(0.0, 0.0, 3.0), angle=0.3), 0
    yield "oblique-circle-taper", encoded(1, small_circle, direction=(1.0, 0.0, 2.0)), 1
    yield "negative-taper", encoded(1, two_circles, angle=-0.1), 1

    square_hole = polygon([(1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)])
    second_hole = polygon([(5.0, 5.0), (6.0, 5.0), (6.0, 6.0), (5.0, 6.0)])
    yield "solid-one-square-hole", encoded(2, [square, square_hole]), 1
    yield "solid-one-loop", encoded(2, [square]), 1
    yield "solid-two-square-holes", encoded(2, [square, square_hole, second_hole]), 1
    yield "solid-circular-hole", encoded(2, [square, inner_circle]), 1
    yield "solid-oblique-direction", encoded(2, [square, square_hole], direction=(1.0, 0.0, 2.0)), 1
    yield "solid-reversed-direction", encoded(2, [square, square_hole], direction=(0.0, 0.0, -2.0)), 1
    yield "solid-outside-hole", encoded(2, [square, polygon([(9.0, 2.0), (10.0, 2.0), (10.0, 3.0), (9.0, 3.0)])]), 0
    yield "solid-intersecting-hole", encoded(2, [square, polygon([(7.0, 2.0), (9.0, 2.0), (9.0, 4.0), (7.0, 4.0)])]), 0
    yield "solid-touching-hole", encoded(2, [square, polygon([(0.0, 2.0), (1.0, 2.0), (1.0, 3.0), (0.0, 3.0)])]), 0
    yield "solid-nested-hole", encoded(2, [square, square_hole, polygon([(1.5, 1.5), (2.0, 1.5), (2.0, 2.0), (1.5, 2.0)])]), 0
    yield "solid-empty-hole", encoded(2, [square, []]), 0
    yield "solid-zero-direction", encoded(2, [square], direction=(0.0, 0.0, 0.0)), 0
    yield "solid-empty", encoded(2, []), 0


def numeric_output(binary: Path, arguments: list[str]) -> tuple[list[float], str]:
    output = process([binary, *arguments], timeout=30)
    try:
        values = [float(token) for token in output.split()]
    except ValueError as error:
        raise AssertionError(f"non-numeric output from {binary}: {output!r}") from error
    if not values or values[0] not in (0.0, 1.0) or not all(map(math.isfinite, values)):
        raise AssertionError(f"invalid output from {binary}: {output!r}")
    if len(values) == 1 and values[0] != 0.0:
        raise AssertionError(f"missing body fields from {binary}")
    if len(values) > 1 and (values[0] != 1.0 or len(values) < 12):
        raise AssertionError(f"unexpected body fields from {binary}")
    return values, output


def compare(name: str, left: list[float], right: list[float], *, exact: bool = False) -> None:
    if len(left) != len(right):
        raise AssertionError(f"{name}: {len(left)} source fields versus {len(right)} native fields")
    for index, (expected, actual) in enumerate(zip(left, right)):
        if exact and expected != actual:
            raise AssertionError(f"{name} field {index}: {actual} != {expected}")
        if not exact and not math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-10):
            raise AssertionError(f"{name} field {index}: native={actual}, Rust={expected}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--rustc", type=Path, default=Path("rustc"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", default="", help="run only cases whose name contains this text")
    parser.add_argument("--exclude-case", default="", help="exclude cases containing this text")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    revision = process(["git", "-c", f"safe.directory={source.as_posix()}",
                        "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    sweep_file = source / "src/brep/sweep.rs"
    if hashlib.sha256(sweep_file.read_bytes()).hexdigest() != SWEEP_HASH:
        raise AssertionError("pinned sweep source changed")
    presspull_file = source / "src/brep/presspull.rs"
    if hashlib.sha256(presspull_file.read_bytes()).hexdigest() != PRESSPULL_HASH:
        raise AssertionError("pinned profile-expansion source changed")
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-region-diff-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ".out"
    binaries: dict[str, dict[str, Path]] = {}
    for mode, directory in (("O0", "debug"), ("O2", "release")):
        library = args.reference_root / directory / "libcadkernel.rlib"
        dependencies = args.reference_root / directory / "deps"
        if not library.is_file() or not dependencies.is_dir():
            raise AssertionError(f"missing pinned reference library: {library}")
        reference = output / f"reference-{mode}{suffix}"
        native = output / f"native-{mode}{suffix}"
        if not args.skip_build:
            rust_diagnostics = process([
                args.rustc, "--edition=2021", HERE / "reference.rs",
                "--extern", f"cadkernel={library.resolve()}",
                "-L", f"dependency={dependencies.resolve()}",
                "-C", f"opt-level={mode[1]}", "-o", reference,
            ])
            native_diagnostics = process([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native])
            if rust_diagnostics:
                print(f"{mode} Rust diagnostics: {rust_diagnostics}", file=sys.stderr)
            if native_diagnostics:
                raise AssertionError(f"{mode}: unexpected native compiler diagnostics\n{native_diagnostics}")
        if not reference.is_file() or not native.is_file():
            raise AssertionError(f"missing {mode} probe binaries")
        binaries[mode] = {"Rust": reference, "DynLex": native}
        print(f"{mode}: probe binaries ready", flush=True)

    count = reference_valid = comparisons = 0
    rust_exact_parity = True
    native_exact_parity = True
    failures: list[str] = []
    for name, values, expected_valid in cases():
        if args.case not in name or (args.exclude_case and args.exclude_case in name):
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        results: dict[tuple[str, str], list[float]] = {}
        texts: dict[tuple[str, str], str] = {}
        case_failures: list[str] = []
        for mode in ("O0", "O2"):
            for language in ("Rust", "DynLex"):
                try:
                    result, text = numeric_output(binaries[mode][language], arguments)
                except AssertionError as error:
                    case_failures.append(f"{name}/{mode}/{language}: {error}")
                    continue
                if result[0] != expected_valid:
                    case_failures.append(
                        f"{name}/{mode}/{language}: expected validity {expected_valid}, got {result[0]}"
                    )
                results[(mode, language)] = result
                texts[(mode, language)] = text
            rust = results.get((mode, "Rust"))
            native = results.get((mode, "DynLex"))
            if rust is not None and native is not None and rust[0] == native[0]:
                try:
                    compare(f"{name}/{mode}", rust, native)
                except AssertionError as error:
                    case_failures.append(str(error))
                else:
                    comparisons += len(rust)
        if ("O0", "DynLex") in texts and ("O2", "DynLex") in texts:
            same = texts[("O0", "DynLex")] == texts[("O2", "DynLex")]
            native_exact_parity &= same
            if not same:
                case_failures.append(f"{name}: native O0/O2 output differs")
        if ("O0", "Rust") in texts and ("O2", "Rust") in texts:
            try:
                compare(f"{name}/Rust O0/O2", results[("O0", "Rust")], results[("O2", "Rust")])
            except AssertionError as error:
                case_failures.append(str(error))
            rust_exact_parity &= texts[("O0", "Rust")] == texts[("O2", "Rust")]
            reference_valid += int(results[("O0", "Rust")][0])
        count += 1
        if case_failures:
            failures.extend(case_failures)
            for failure in case_failures:
                print(f"FAIL {failure}", flush=True)
        else:
            print(f"PASS {name}: {len(results[('O0', 'Rust')])} fields per mode", flush=True)

    if count == 0 or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty case selection or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "cases": count,
        "reference_valid_cases": reference_valid,
        "field_comparisons": comparisons,
        "case_filter": args.case,
        "excluded_case": args.exclude_case,
        "native_exact_optimization_parity": native_exact_parity,
        "rust_exact_optimization_parity": rust_exact_parity,
        "failures": failures,
        "source_sha256": {"src/brep/sweep.rs": SWEEP_HASH, "src/brep/presspull.rs": PRESSPULL_HASH},
        "reference_library_sha256": {
            mode: hashlib.sha256((args.reference_root / directory / "libcadkernel.rlib").read_bytes()).hexdigest()
            for mode, directory in (("O0", "debug"), ("O2", "release"))
        },
    }
    summary_name = "filtered-summary.json" if args.case or args.exclude_case else "summary.json"
    (output / summary_name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if failures:
        raise AssertionError(f"{len(failures)} differential checks failed; artifacts={output}")
    print(f"PASS: {count} cases, {comparisons} field comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
