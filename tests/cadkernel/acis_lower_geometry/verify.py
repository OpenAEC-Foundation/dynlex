#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare native geometry lowering with pinned kernel and codec records."""
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
from verify import compare, normalize_output, run_process, verify_fixture

SOURCE_PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
CODEC_PIN = "70ac6da7cf149cea6398a3d8829dd5e48b485b96"


def checked(command: list[str | Path], *, cwd: Path, timeout: float = 180) -> str:
    status, output, _ = run_process([str(item) for item in command], timeout=timeout, cwd=cwd)
    if status:
        raise RuntimeError(f"exit {status}: {' '.join(map(str, command))}\n{output}")
    return output


def pin(path: Path, expected: str) -> None:
    actual = checked(
        ["git", "-c", f"safe.directory={path.as_posix()}", "-C", path, "rev-parse", "HEAD"],
        cwd=ROOT,
    ).strip()
    if actual != expected:
        raise RuntimeError(f"source revision mismatch at {path}: {actual} != {expected}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--codec", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    args = parser.parse_args()
    source, codec, compiler = (p.resolve() for p in (args.source, args.codec, args.compiler))
    pin(source, SOURCE_PIN)
    pin(codec, CODEC_PIN)
    if shutil.which("cargo") is None:
        raise RuntimeError("cargo is required")

    parent = ROOT / "build/cadkernel-acis-lower-checks"
    parent.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    project = artifacts / "reference"
    (project / "src").mkdir(parents=True)
    (project / "src/main.rs").write_bytes((HERE / "reference.rs").read_bytes())
    (project / "Cargo.toml").write_text(
        '[package]\nname = "cadkernel_acis_lower_reference"\nversion = "0.1.0"\nedition = "2021"\n'
        f'\n[dependencies]\ncadkernel = {{ path = "{source.as_posix()}", features = ["acis"] }}\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n'
        f'\n[patch."https://github.com/HakanSeven12/cadcodec.git"]\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n',
        encoding="utf-8",
    )
    target = parent / "target"
    cargo = ["cargo"]
    if sys.platform == "win32":
        cargo.append("+stable-x86_64-pc-windows-msvc")
    checked(
        [*cargo, "build", "--offline", "--manifest-path", project / "Cargo.toml", "--target-dir", target],
        cwd=ROOT,
        timeout=420,
    )
    reference = target / "debug/cadkernel_acis_lower_reference.exe"
    if not reference.is_file():
        reference = reference.with_suffix("")
    expected = normalize_output(checked([reference], cwd=ROOT))
    fixture = ROOT / "tests/required/cadkernel_acis_lower_geometry"
    compare("pinned source vs fixture oracle", normalize_output((fixture / "expected.txt").read_text(encoding="utf-8")), expected)

    for mode in ("O0", "O2"):
        result = verify_fixture(fixture, mode, compiler, artifacts, 240, 30, False)
        actual = normalize_output(checked([artifacts / f"cadkernel_acis_lower_geometry-{mode}.out"], cwd=ROOT))
        compare(f"pinned source vs native {mode}", expected, actual)
        print(f"{mode}: {result}; {len(expected.splitlines())} differential lines match", flush=True)
    print(f"Source pins: {SOURCE_PIN}, {CODEC_PIN}")
    print(f"Compiler SHA256: {hashlib.sha256(compiler.read_bytes()).hexdigest()}")
    print(f"Artifacts: {artifacts}")


if __name__ == "__main__":
    main()
