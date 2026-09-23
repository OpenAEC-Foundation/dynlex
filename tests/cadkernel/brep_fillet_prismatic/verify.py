#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native prismatic filleting with the pinned source implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import runpy
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_fillet_prismatic"
PRISMATIC = runpy.run_path(str(ROOT / "tests/cadkernel/brep_chamfer_prismatic/verify.py"))


def process(command, *, timeout=240):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def arguments(values):
    result = []
    for value in values:
        if isinstance(value, float):
            result.append(format(value, ".17g"))
        else:
            result.append(str(value))
    return result


def cases():
    seen = set()
    for source_name, values in PRISMATIC["cases"]():
        radius = values[13]
        converted = [*values[:12], radius, values[15], values[16]]
        key = tuple(arguments(converted))
        if key in seen:
            continue
        seen.add(key)
        yield source_name, converted

    encoded = PRISMATIC["encoded"]
    for edge in range(12):
        for radius in (1e-12, 1e-6, 0.05, 0.5, 1.0, 1.999999999, 2.0, 3.999999999, 4.0, 20.0):
            values = encoded(selected=(edge,), base=radius, other=radius)
            yield f"fillet-cuboid-edge-{edge}-radius-{radius}", [
                *values[:12], radius, values[15], values[16],
            ]


def parsed(output):
    values = []
    for line in output.splitlines():
        if re.fullmatch(r"[+-]?nan(?:\([a-zA-Z0-9_]+\))?", line, re.IGNORECASE):
            values.append(math.nan)
        else:
            values.append(float(line))
    if len(values) < 3 or values[0] not in (0.0, 1.0) or values[1] not in (0.0, 1.0):
        raise AssertionError(f"invalid output: {output!r}")
    return values


def same_float(actual, expected):
    if math.isnan(actual) or math.isnan(expected):
        return math.isnan(actual) and math.isnan(expected)
    if actual == expected:
        return actual != 0.0 or math.copysign(1.0, actual) == math.copysign(1.0, expected)
    if math.isinf(actual) or math.isinf(expected):
        return False
    return math.isclose(actual, expected, rel_tol=5e-12, abs_tol=5e-10)


def compare(name, actual, expected, mode):
    if len(actual) != len(expected):
        raise AssertionError(
            f"{name}/{mode}: {len(actual)} native fields != {len(expected)} Rust fields"
        )
    for index, (native, rust) in enumerate(zip(actual, expected)):
        if not same_float(native, rust):
            raise AssertionError(
                f"{name}/{mode} field {index}: native={native!r}, Rust={rust!r}"
            )
    return len(actual)


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
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD",
    ])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    source_files = [
        source / "src/brep/fillet_circular.rs",
        source / "src/brep/fillet_prismatic.rs",
    ]
    process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
        "diff", "--exit-code", PIN, "--", *(path.relative_to(source) for path in source_files),
    ])
    if any("#[test]" in path.read_text(encoding="utf-8") for path in source_files):
        raise AssertionError("unexpected source tests in private fillet modules")

    output = args.output or Path(tempfile.mkdtemp(prefix="brep-fillet-prismatic-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    reference_source = output / "reference.rs"
    shutil.copy2(HERE / "reference.rs", reference_source)
    shutil.copy2(source_files[0], output / "fillet_circular_source.rs")
    shutil.copy2(source_files[1], output / "fillet_prismatic_source.rs")

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
            diagnostics, native_seconds = process([
                args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
            ])
            if diagnostics:
                raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
            binaries[mode] = {"rust": reference, "native": native}
            compile_times[mode] = {"rust": rust_seconds, "native": native_seconds}
            print(f"{mode}: Rust={rust_seconds:.3f}s native={native_seconds:.3f}s", flush=True)
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

    count = recognized = valid = comparisons = 0
    exact_rust_optimization_parity = True
    for name, values in cases():
        if args.case not in name:
            continue
        command_arguments = arguments(values)
        raw = {}
        outputs = {}
        for mode in ("O0", "O2"):
            for implementation in ("rust", "native"):
                text, _ = process([binaries[mode][implementation], *command_arguments], timeout=30)
                raw[(mode, implementation)] = text
                outputs[(mode, implementation)] = parsed(text)
        if raw[("O0", "native")] != raw[("O2", "native")]:
            raise AssertionError(f"{name}: native optimization mismatch")
        if raw[("O0", "rust")] != raw[("O2", "rust")]:
            exact_rust_optimization_parity = False
            first = outputs[("O0", "rust")]
            second = outputs[("O2", "rust")]
            if len(first) != len(second):
                raise AssertionError(f"{name}: Rust optimization field count mismatch")
            for field, (left, right) in enumerate(zip(first, second)):
                if not same_float(left, right):
                    raise AssertionError(
                        f"{name}: Rust optimization field {field}: O0={left!r}, O2={right!r}"
                    )
        for mode in ("O0", "O2"):
            comparisons += compare(name, outputs[(mode, "native")], outputs[(mode, "rust")], mode)
        expected = outputs[("O0", "rust")]
        recognized += int(expected[0])
        valid += int(expected[1])
        count += 1
        if count % 50 == 0:
            print(f"{count} cases passed ({name})", flush=True)

    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 0,
        "native_groups": len((HERE / "expected.txt").read_text(encoding="utf-8").splitlines()),
        "cases": count,
        "recognized_cases": recognized,
        "valid_cases": valid,
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
        "source_sha256": {
            str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in source_files
        },
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                ROOT / "lib/cadkernel/brep_fillet_profile.dl",
                ROOT / "lib/cadkernel/brep_fillet_prismatic.dl",
                HERE / "main.dl",
                HERE / "probe.dl",
            )
        },
    }
    summary_name = "filtered-summary.json" if args.case else "summary.json"
    (output / summary_name).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
