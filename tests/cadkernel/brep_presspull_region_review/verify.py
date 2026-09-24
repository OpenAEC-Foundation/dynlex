#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare region winding and hole refusal with the pinned CAD kernel."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SOURCE_HASHES = {
    "src/brep/presspull.rs": "273ab904d7b85b3e25b85d467b7dbd12397e1fe5ed2b229a47c7ca2a89f3c96d",
    "src/brep/sweep.rs": "492ac57683c6681f6e84f9fbbbdee2001b2b18a2bd766eb714588f90e4ffb995",
}
CASES = (
    "outer-ccw", "outer-cw", "hole-ccw", "outer-and-hole-cw", "outer-cw-hole-ccw",
    "hole-outside", "hole-touching", "hole-crossing", "holes-nested",
    "holes-overlapping", "holes-disjoint",
)


def run(*command: object, timeout: int = 240) -> str:
    status, output, _ = fixtures.run_process(
        [str(part) for part in command], cwd=ROOT, timeout=timeout,
    )
    if status:
        raise AssertionError(f"exit {status}: {command}\n{output}")
    return output.strip()


def observe(output: str) -> tuple[int, ...] | tuple[tuple[int, ...], tuple[float, ...]]:
    values = [float(value) for value in output.split()]
    if values == [0.0]:
        return (0,)
    if len(values) < 4 or values[0] != 1.0:
        raise AssertionError(f"invalid probe output: {output!r}")
    flaws, forward, loops = (int(value) for value in values[1:4])
    if len(values) != 4 + loops or loops < 1:
        raise AssertionError(f"incomplete ring data: {output!r}")
    return (1, flaws, forward, loops), tuple(values[4:])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--reference-root", type=Path, default=ROOT / "build/cadkernel-upstream-offset-reference")
    parser.add_argument("--mode", choices=("O0", "O2"), action="append")
    args = parser.parse_args()
    source = args.source.resolve()
    revision = run("git", "-c", f"safe.directory={source.as_posix()}", "-C", source, "rev-parse", "HEAD")
    if revision != PIN:
        raise AssertionError(f"source revision {revision} != {PIN}")
    for path, digest in SOURCE_HASHES.items():
        actual = hashlib.sha256((source / path).read_bytes()).hexdigest()
        if actual != digest:
            raise AssertionError(f"source hash changed: {path} {actual}")
    modes = tuple(dict.fromkeys(args.mode or ("O0", "O2")))
    suffix = ".exe" if sys.platform == "win32" else ".out"
    with tempfile.TemporaryDirectory(prefix="cad-region-review-") as directory:
        temporary = Path(directory)
        for mode in modes:
            folder = "debug" if mode == "O0" else "release"
            library = (args.reference_root / folder / "libcadkernel.rlib").resolve()
            dependencies = (args.reference_root / folder / "deps").resolve()
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library: {library}")
            rust_binary = temporary / f"rust-{mode}{suffix}"
            native_binary = temporary / f"dynlex-{mode}{suffix}"
            run("rustc", "--edition=2021", HERE / "reference.rs", "--extern",
                f"cadkernel={library}", "-L", f"dependency={dependencies}",
                "-C", f"opt-level={mode[1]}", "-o", rust_binary)
            diagnostics = run(args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary)
            if diagnostics:
                raise AssertionError(f"DynLex diagnostics {mode}: {diagnostics}")
            failures = []
            for index, case in enumerate(CASES):
                expected = observe(run(rust_binary, index, timeout=30))
                actual = observe(run(native_binary, index, timeout=30))
                if expected[0] != actual[0] or (len(expected) > 1 and (expected[0] != actual[0] or any(
                    not math.isclose(left, right, rel_tol=1e-10, abs_tol=1e-8)
                    for left, right in zip(expected[1], actual[1])
                ))):
                    failures.append(f"{case}/{mode}: Rust={expected}, DynLex={actual}")
                else:
                    print(f"PASS {case}/{mode}", flush=True)
            if failures:
                raise AssertionError("\n".join(failures))
    print(f"PASS: {len(CASES)} cases at {', '.join(modes)}")


if __name__ == "__main__":
    main()
