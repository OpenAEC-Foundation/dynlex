#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native analytic mass properties with the pinned source implementation."""
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
HERE = ROOT / "tests/cadkernel/brep_mass"


def process(command, timeout=180):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def mass_case(kind, origin, a, b, c, d, delta):
    return [kind, *origin, a, b, c, d, *delta]


def cases():
    yield "source-sphere", mass_case(0, (2, -3, 5), 2, 1, 1, 1, (0, 0, 0))
    yield "source-cylinder", mass_case(1, (1, 2, 3), 2, 6, 1, 1, (0, 0, 0))
    yield "translated-sphere", mass_case(0, (2, -3, 5), 2, 1, 1, 1, (7, 11, -13))
    yield "quarter-sector", mass_case(4, (0, 0, 0), 3, 1, 4, math.pi / 2, (0, 0, 0))
    yield "hollow-sphere", mass_case(5, (4, -2, 9), 3, 1, 1, 1, (0, 0, 0))
    yield "unsupported-box", mass_case(3, (0, 0, 0), 2, 3, 4, 1, (0, 0, 0))
    yield "unsupported-cone", mass_case(2, (0, 0, 0), 2, 5, 1, 1, (0, 0, 0))
    rng = random.Random(864209753)
    for index in range(240):
        kind = index % 6
        scale = 10.0 ** rng.uniform(-2.0, 2.0)
        origin_scale = 1_000_000.0 if index % 17 == 0 else 100.0
        origin = tuple(rng.uniform(-origin_scale, origin_scale) for _ in range(3))
        a = rng.uniform(0.2, 6.0) * scale
        b = rng.uniform(0.1, 0.9) * a if kind in (4, 5) else rng.uniform(0.2, 8.0) * scale
        c = rng.uniform(0.2, 8.0) * scale
        d = rng.uniform(0.15, 2.0 * math.pi)
        delta = tuple(rng.uniform(-50.0, 50.0) for _ in range(3))
        yield f"random-{kind}-{index}", mass_case(kind, origin, a, b, c, d, delta)


def parsed(output):
    lines = output.splitlines()
    if not lines or lines[0] not in ("0", "1"):
        raise AssertionError(f"invalid output: {output!r}")
    if lines[0] == "0":
        if len(lines) != 1:
            raise AssertionError(f"invalid absent output: {output!r}")
        return (False,)
    values = tuple(float(value) for value in lines[1:])
    if len(values) != 25:
        raise AssertionError(f"expected 25 mass fields, got {len(values)}: {output!r}")
    return (True, *values)


def same_float(actual, expected):
    if math.isnan(actual) or math.isnan(expected):
        return math.isnan(actual) and math.isnan(expected)
    if math.isinf(actual) or math.isinf(expected):
        return actual == expected
    return math.isclose(actual, expected, rel_tol=5e-12, abs_tol=5e-10)


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
    args = parser.parse_args()

    source = args.source.resolve()
    revision, _ = process([
        "git", "-c", f"safe.directory={source.as_posix()}", "-C", source,
        "rev-parse", "HEAD",
    ])
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    output = args.output or Path(tempfile.mkdtemp(prefix="brep-mass-", dir=ROOT / "build"))
    output.mkdir(parents=True, exist_ok=True)
    compiler_hash = hashlib.sha256(args.compiler.read_bytes()).hexdigest()
    suffix = ".exe" if sys.platform == "win32" else ".out"
    binaries = {}
    compile_times = {}
    for mode in ("O0", "O2"):
        library = args.libraries / f"libcadkernel-{mode}.rlib"
        if not library.is_file():
            raise AssertionError(f"missing reference library: {library}")
        reference = output / f"reference-{mode}{suffix}"
        native = output / f"native-{mode}{suffix}"
        diagnostics, rust_time = process([
            args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
            f"cadkernel={library.resolve()}", "-L", f"dependency={args.dependencies.resolve()}",
            "-C", f"opt-level={mode[1]}", "-o", reference,
        ])
        if diagnostics:
            raise AssertionError(f"unexpected Rust diagnostics: {diagnostics}")
        diagnostics, native_time = process([
            args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
        ])
        if diagnostics:
            raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
        binaries[mode] = {"rust": reference, "native": native}
        compile_times[mode] = {"rust": rust_time, "native": native_time}
        print(f"{mode} compiled: Rust {rust_time:.3f}s, native {native_time:.3f}s", flush=True)

    count = comparisons = 0
    valid_cases = 0
    exact_rust_optimization_parity = True
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        outputs = {}
        raw = {}
        for mode in ("O0", "O2"):
            for implementation in ("rust", "native"):
                text, _ = process([binaries[mode][implementation], *arguments], timeout=30)
                raw[(mode, implementation)] = text
                outputs[(mode, implementation)] = parsed(text)
        if raw[("O0", "native")] != raw[("O2", "native")]:
            raise AssertionError(f"{name}: native optimization mismatch")
        if raw[("O0", "rust")] != raw[("O2", "rust")]:
            exact_rust_optimization_parity = False
        expected = outputs[("O0", "rust")]
        if any(output[0] != expected[0] for output in outputs.values()):
            raise AssertionError(f"{name}: validity mismatch {outputs}; input={arguments}")
        comparisons += 4
        if expected[0]:
            valid_cases += 1
            for field in range(1, len(expected)):
                if not same_float(
                    outputs[("O0", "rust")][field],
                    outputs[("O2", "rust")][field],
                ):
                    raise AssertionError(
                        f"{name}: field {field}: Rust optimization mismatch; "
                        f"input={arguments}"
                    )
                if not same_float(
                    outputs[("O0", "native")][field],
                    outputs[("O2", "native")][field],
                ):
                    raise AssertionError(
                        f"{name}: field {field}: native optimization mismatch; "
                        f"input={arguments}"
                    )
                for mode in ("O0", "O2"):
                    actual = outputs[(mode, "native")][field]
                    reference = outputs[(mode, "rust")][field]
                    if not same_float(actual, reference):
                        raise AssertionError(
                            f"{name}: field {field} {mode}: native={actual!r}, Rust={reference!r}; "
                            f"input={arguments}"
                        )
                    comparisons += 1
                comparisons += 2
        count += 1
        if count % 50 == 0:
            print(f"{count} cases passed ({name})", flush=True)
    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 3,
        "native_groups": 6,
        "cases": count,
        "valid_cases": valid_cases,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": True,
        "exact_rust_optimization_parity": exact_rust_optimization_parity,
        "numeric_rust_optimization_parity": True,
        "compile_seconds": compile_times,
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (
                ROOT / "lib/cadkernel/brep_mass.dl", HERE / "main.dl",
                HERE / "probe.dl", HERE / "support.dl",
            )
        },
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS: {count} cases / {comparisons} comparisons; artifacts={output}",
        flush=True,
    )


if __name__ == "__main__":
    main()
