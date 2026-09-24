#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare the native Cylinder route with pinned public Rust history at O0/O2."""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import runpy
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
from verify import run_process, verify_fixture

CROSS = runpy.run_path(str(ROOT / "tests/cadkernel/cross2/verify.py"))
CURVE, MEASURE = CROSS["CURVE"], CROSS["MEASURE"]

SOURCE_PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SOURCE_SHA256 = "de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc"
CODEC_PIN = "70ac6da7cf149cea6398a3d8829dd5e48b485b96"
CODEC_SHA256 = "17bdb6a8855527ed478a17019832ac20434d54e68d7db9d817fad29b9c677697"


def checked(command: list[str | Path], *, cwd: Path, timeout: float = 180) -> str:
    status, output, _ = run_process([str(part) for part in command], timeout=timeout, cwd=cwd)
    if status:
        raise RuntimeError(f"exit {status}: {' '.join(map(str, command))}\n{output}")
    return output


def pin(path: Path, revision: str, relative: str, digest: str) -> None:
    actual = checked(["git", "-c", f"safe.directory={path.as_posix()}", "-C", path,
                      "rev-parse", "HEAD"], cwd=ROOT).strip()
    if actual != revision:
        raise RuntimeError(f"source revision mismatch: {actual} != {revision}")
    actual_hash = hashlib.sha256((path / relative).read_bytes()).hexdigest()
    if actual_hash != digest:
        raise RuntimeError(f"source file hash mismatch: {relative}: {actual_hash}")
    checked(["git", "-c", f"safe.directory={path.as_posix()}", "-C", path,
             "diff", "--exit-code", "HEAD", "--", "src", "Cargo.toml"], cwd=ROOT)


def matrix(x=(1., 0., 0.), y=(0., 1., 0.), z=(0., 0., 1.),
           origin=(0., 0., 0.), column=(0., 0., 0., 1.)) -> list[float]:
    return [*x, column[0], *y, column[1], *z, column[2], *origin, column[3]]


IDENTITY = matrix()
TURN = matrix((0., 1., 0.), (-1., 0., 0.), (0., 0., 1.), (5., -2., 3.))
SCALE = matrix((2., 0., 0.), (0., 2., 0.), (0., 0., 2.), (1., 2., 3.))
REFLECT = matrix((-1., 0., 0.), (0., 1., 0.), (0., 0., 1.), (4., 5., 6.))
SHEAR = matrix((1., 0., 0.), (.25, 1., 0.), (0., 0., 1.))
NONUNIFORM = matrix((1., 0., 0.), (0., 2., 0.), (0., 0., 1.))
ZERO = matrix((0., 0., 0.), (0., 1., 0.), (0., 0., 1.))


def cases():
    for name, dimensions in (("circular", (3., 3., 7., 5.)),
                             ("elliptic", (3., 2., 7., 5.)),
                             ("near-circular", (3., 3. + 2e-13, 7., 5.)),
                             ("just-elliptic", (3., 3. + 1e-11, 7., 5.))):
        for placement_name, placement in (("identity", IDENTITY), ("turn", TURN),
                                          ("scale", SCALE), ("reflect", REFLECT)):
            yield f"{name}-{placement_name}", dimensions, placement, 0
    yield "survey", (2.5, 1.5, 9., 3.5), matrix(origin=(512345.678, 4512345.678, 91.5)), 0
    yield "tiny-x-radius", (3., 2., 5e-324, 5.), IDENTITY, 0
    yield "large-x-radius", (3., 2., 1e150, 5.), IDENTITY, 0
    for label, dimensions in (
        ("zero-major", (0., 2., 7., 5.)),
        ("negative-minor", (3., -2., 7., 5.)),
        ("zero-x-radius", (3., 2., 0., 5.)),
        ("nan-x-radius", (3., 2., math.nan, 5.)),
        ("infinite-major", (math.inf, 2., 7., 5.)),
        ("zero-height", (3., 2., 7., 0.)),
        ("negative-height", (3., 2., 7., -5.)),
        ("nan-height", (3., 2., 7., math.nan)),
        ("infinite-height", (3., 2., 7., math.inf)),
    ):
        yield label, dimensions, IDENTITY, 2
    for label, placement in (
        ("bad-column-3", matrix(column=(2e-9, 0., 0., 1.))),
        ("bad-column-7", matrix(column=(0., -2e-9, 0., 1.))),
        ("bad-column-11", matrix(column=(0., 0., 2e-9, 1.))),
        ("bad-last", matrix(column=(0., 0., 0., 1. + 2e-9))),
        ("nan-origin", matrix(origin=(math.nan, 0., 0.))),
        ("infinite-origin", matrix(origin=(math.inf, 0., 0.))),
        ("shear", SHEAR), ("nonuniform", NONUNIFORM), ("zero-scale", ZERO),
    ):
        yield label, (3., 2., 7., 5.), placement, 1
    yield "invalid-x-radius-wins", (3., 2., 0., 5.), SHEAR, 2
    yield "invalid-height-wins", (3., 2., 7., 0.), SHEAR, 2
    for index in range(6):
        dimensions = (2. + index / 8., 1. + index / 16., 7. + index, 3. + index / 4.)
        placement = matrix((0., 1., 0.), (-1., 0., 0.), (0., 0., 1.),
                           (index - 3., 2. * index, -index / 2.))
        yield f"elliptic-placed-{index}", dimensions, placement, 0


def compare_trace(label: str, reference: str, native: str) -> int:
    expected, limits = MEASURE["expected_values"](reference)
    actual = CURVE["parsed"](native)
    if len(actual) != len(expected):
        raise AssertionError(f"{label}: {len(actual)} fields != {len(expected)} Rust fields")
    for index, (value, target, limit) in enumerate(zip(actual, expected, limits)):
        if not CURVE["equal"](value, target, exact=limit < 0, absolute=max(0., limit)):
            raise AssertionError(f"{label}: field {index}: native {value} != Rust {target}")
    return len(expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--codec", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    args = parser.parse_args()
    source, codec, compiler = (path.resolve() for path in (args.source, args.codec, args.compiler))
    pin(source, SOURCE_PIN, "src/acis/history.rs", SOURCE_SHA256)
    pin(codec, CODEC_PIN, "src/objects/dynamic_block.rs", CODEC_SHA256)
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    if shutil.which("cargo") is None:
        raise RuntimeError("cargo is required")

    parent = ROOT / "build/cadkernel-acis-history-cylinder-checks"
    parent.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    project = artifacts / "reference"
    (project / "src").mkdir(parents=True)
    (project / "src/main.rs").write_bytes((HERE / "reference.rs").read_bytes())
    (project / "src/emit.rs").write_bytes((ROOT / "tests/cadkernel/brep_make/emit.rs").read_bytes())
    (project / "Cargo.toml").write_text(
        '[package]\nname = "cadkernel_acis_history_cylinder_reference"\nversion = "0.1.0"\nedition = "2021"\n'
        f'\n[dependencies]\ncadkernel = {{ path = "{source.as_posix()}", features = ["acis"] }}\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n'
        '\n[patch."https://github.com/HakanSeven12/cadcodec.git"]\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n'
        '\n[profile.dev]\nopt-level = 0\n'
        '\n[profile.release]\nopt-level = 2\nlto = false\ncodegen-units = 16\n',
        encoding="utf-8",
    )
    cargo = ["cargo"]
    if sys.platform == "win32":
        cargo.append("+stable-x86_64-pc-windows-msvc")
    target = parent / "target"
    fixture = ROOT / "tests/required/cadkernel_acis_history_cylinder"
    count = fields = 0
    for mode in ("O0", "O2"):
        build = [*cargo, "build", "--offline", "--manifest-path", project / "Cargo.toml",
                 "--target-dir", target]
        if mode == "O2":
            build.append("--release")
        checked(build, cwd=ROOT, timeout=1200)
        reference = target / ("release" if mode == "O2" else "debug") / "cadkernel_acis_history_cylinder_reference.exe"
        if not reference.is_file():
            reference = reference.with_suffix("")
        print(f"{mode}: {verify_fixture(fixture, mode, compiler, artifacts, 240, 30, False)}", flush=True)
        native = artifacts / f"differential-{mode}.out"
        checked([compiler, HERE / "differential.dl", f"-{mode}", "-o", native],
                cwd=ROOT, timeout=240)
        mode_count = 0
        for label, dimensions, matrix_values, expected_failure in cases():
            values = [*(str(value) for value in dimensions), *(str(value) for value in matrix_values)]
            output = checked([native, "body", *values], cwd=ROOT, timeout=30)
            for route in ("body", "history"):
                rust = checked([reference, route, *values], cwd=ROOT, timeout=30)
                actual_failure = int(float(rust.splitlines()[0].split()[-1]))
                if actual_failure != expected_failure:
                    raise AssertionError(f"{label}/{route}/{mode}: Rust failure {actual_failure} != {expected_failure}")
                fields += compare_trace(f"{label}/{route}/{mode}", rust, output)
            count += 1
            mode_count += 1
        print(f"{mode}: {mode_count} cases match public Rust body and single-history routes", flush=True)
    if hashlib.sha256(compiler.read_bytes()).hexdigest() != compiler_hash:
        raise RuntimeError("compiler changed during verification")
    print(f"PASS: {count} cases across O0/O2, {fields} compared fields")
    print(f"Source: {SOURCE_PIN}; history.rs SHA256: {SOURCE_SHA256}")
    print(f"Codec: {CODEC_PIN}; dynamic_block.rs SHA256: {CODEC_SHA256}")
    print(f"Compiler SHA256: {compiler_hash}")
    print(f"Artifacts: {artifacts}")


if __name__ == "__main__":
    main()
