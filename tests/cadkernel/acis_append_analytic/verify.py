#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare complete analytic SAT records with the pinned kernel and codec."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
from verify import compare, normalize_output, run_process

SOURCE_PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
CODEC_PIN = "70ac6da7cf149cea6398a3d8829dd5e48b485b96"
CASES = range(6)


def checked(command: list[str | Path], *, timeout: float = 240) -> str:
    status, output, _ = run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if status:
        raise AssertionError(f"exit {status}: {' '.join(map(str, command))}\n{output}")
    return normalize_output(output)


def pin(path: Path, wanted: str) -> None:
    actual = checked(["git", "-c", f"safe.directory={path.as_posix()}", "-C", path, "rev-parse", "HEAD"])
    if actual != wanted:
        raise AssertionError(f"pin mismatch at {path}: {actual} != {wanted}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--codec", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex-expression-fix-v2.exe")
    parser.add_argument("--mode", action="append", choices=("O0", "O2"))
    parser.add_argument("--case", action="append", type=int)
    parser.add_argument("--reference-only", action="store_true")
    args = parser.parse_args()
    source, codec, compiler = (item.resolve() for item in (args.source, args.codec, args.compiler))
    pin(source, SOURCE_PIN)
    pin(codec, CODEC_PIN)
    if shutil.which("cargo") is None:
        raise AssertionError("cargo is required")
    cases = sorted(set(args.case if args.case is not None else CASES))
    if not cases or any(case not in CASES for case in cases):
        raise AssertionError(f"invalid cases: {cases}")
    parent = ROOT / "build/cadkernel-acis-append-analytic-checks"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as scratch:
        output = Path(scratch)
        project = output / "reference"
        (project / "src").mkdir(parents=True)
        (project / "src/main.rs").write_bytes((HERE / "reference.rs").read_bytes())
        (project / "Cargo.toml").write_text(
            '[package]\nname = "cadkernel_acis_append_analytic_reference"\nversion = "0.1.0"\nedition = "2021"\n'
            f'\n[dependencies]\ncadkernel = {{ path = "{source.as_posix()}", features = ["acis"] }}\n'
            f'acadrust = {{ path = "{codec.as_posix()}" }}\n'
            '\n[patch."https://github.com/HakanSeven12/cadcodec.git"]\n'
            f'acadrust = {{ path = "{codec.as_posix()}" }}\n', encoding="utf-8",
        )
        cargo = ["cargo"]
        if sys.platform == "win32":
            cargo.append("+stable-x86_64-pc-windows-msvc")
        target = parent / "target"
        checked([*cargo, "build", "--offline", "--manifest-path", project / "Cargo.toml",
                 "--target-dir", target], timeout=600)
        suffix = ".exe" if sys.platform == "win32" else ""
        rust = target / f"debug/cadkernel_acis_append_analytic_reference{suffix}"
        expected = {case: checked([rust, str(case)], timeout=30) for case in cases}
        if args.reference_only:
            for case in cases:
                lines = expected[case].splitlines()
                print(f"RUST case-{case}: {lines[0]}; records={len(lines)-1}", flush=True)
            return
        for mode in dict.fromkeys(args.mode or ("O0", "O2")):
            binary = output / f"native-{mode}{suffix}"
            diagnostics = checked([compiler, HERE / "probe.dl", f"-{mode}", "-o", binary])
            if diagnostics:
                raise AssertionError(f"DynLex diagnostics {mode}: {diagnostics}")
            for case in cases:
                actual = checked([binary, str(case)], timeout=30)
                compare(f"complete SAT records case-{case}/{mode}", expected[case], actual)
                print(f"PASS case-{case}/{mode}: {len(actual.splitlines())-1} records", flush=True)
    print(f"PASS: {len(cases)} cases at {', '.join(dict.fromkeys(args.mode or ('O0', 'O2')))}")
    print(f"Compiler SHA256: {hashlib.sha256(compiler.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
