#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native solid containment with the pinned source implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
HERE = ROOT / "tests/cadkernel/brep_classify"


def process(command, timeout=300):
    status, output, elapsed = fixtures.run_process(
        [str(part) for part in command], timeout=timeout, cwd=ROOT,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip(), elapsed


def case(kind, origin, a, b, c, sides, point, tolerance):
    return [kind, *origin, a, b, c, sides, *point, tolerance]


def cases():
    tolerance = 1e-9
    yield "box-inside", case(0, (0, 0, 0), 10, 10, 10, 6, (5, 5, 5), tolerance)
    yield "box-outside", case(0, (0, 0, 0), 10, 10, 10, 6, (20, 5, 5), tolerance)
    yield "box-edge-extension-outside", case(0, (0, 0, 10), 4, 4, 4, 6, (4, 4, 0), tolerance)
    yield "box-boundary", case(0, (0, 0, 0), 10, 10, 10, 6, (0, 5, 5), 1e-6)
    yield "cylinder-inside", case(1, (10, 5, -2), 3, 14, 0, 6, (10, 5, 5), tolerance)
    yield "cylinder-boundary", case(1, (10, 5, -2), 3, 14, 0, 6, (13, 5, 5), tolerance)
    yield "unsupported", case(6, (0, 0, 0), 10, 10, 10, 6, (5, 5, 5), tolerance)
    yield "survey", case(
        0, (512345.678, 4512345.678, 91.5), 4, 4, 4, 6,
        (512347.678, 4512347.678, 93.5), 1e-6,
    )
    rng = random.Random(951753)
    for index in range(240):
        kind = index % 6
        origin = tuple(rng.uniform(-100.0, 100.0) for _ in range(3))
        a = rng.uniform(0.2, 8.0)
        b = rng.uniform(0.2, 8.0)
        c = rng.uniform(0.1, a) if kind == 5 else rng.uniform(0.2, 8.0)
        sides = rng.randrange(3, 10)
        scale = max(a, b, c)
        point = tuple(origin[axis] + rng.uniform(-1.75, 1.75) * scale for axis in range(3))
        tolerance = (1e-12, 1e-9, 1e-6)[index % 3]
        yield f"random-{kind}-{index}", case(
            kind, origin, a, b, c, sides, point, tolerance,
        )


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
    output = args.output or Path(tempfile.mkdtemp(prefix="brep-classify-", dir=ROOT / "build"))
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
        _, rust_time = process([
            args.rustc, "--edition=2021", HERE / "reference.rs", "--extern",
            f"cadkernel={library.resolve()}", "-L", f"dependency={args.dependencies.resolve()}",
            "-C", f"opt-level={mode[1]}", "-o", reference,
        ])
        diagnostics, native_time = process([
            args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native,
        ])
        if diagnostics:
            raise AssertionError(f"unexpected native diagnostics: {diagnostics}")
        binaries[mode] = {"rust": reference, "native": native}
        compile_times[mode] = {"rust": rust_time, "native": native_time}
        print(f"{mode} compiled: Rust {rust_time:.3f}s, native {native_time:.3f}s", flush=True)

    count = comparisons = 0
    for name, values in cases():
        if args.case not in name:
            continue
        arguments = [format(value, ".17g") if isinstance(value, float) else str(value) for value in values]
        outputs = {}
        for mode in ("O0", "O2"):
            for implementation in ("rust", "native"):
                text, _ = process([binaries[mode][implementation], *arguments], timeout=30)
                outputs[(mode, implementation)] = int(text)
        expected = outputs[("O0", "rust")]
        if any(value != expected for value in outputs.values()):
            raise AssertionError(f"{name}: {outputs}; input={arguments}")
        count += 1
        comparisons += 4
        if count % 50 == 0:
            print(f"{count} cases passed ({name})", flush=True)
    if not count or hashlib.sha256(args.compiler.read_bytes()).hexdigest() != compiler_hash:
        raise AssertionError("empty run or compiler changed during verification")
    summary = {
        "revision": PIN,
        "compiler_sha256": compiler_hash,
        "source_tests": 14,
        "native_groups": 14,
        "cases": count,
        "comparisons": comparisons,
        "case_filter": args.case,
        "exact_native_optimization_parity": True,
        "exact_rust_optimization_parity": True,
        "compile_seconds": compile_times,
        "native_source_sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (ROOT / "lib/cadkernel/brep_classify.dl", HERE / "main.dl", HERE / "probe.dl")
        },
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {count} cases / {comparisons} exact comparisons; artifacts={output}", flush=True)


if __name__ == "__main__":
    main()
