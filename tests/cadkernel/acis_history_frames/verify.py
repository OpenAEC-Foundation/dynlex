#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare isolated history frame helpers with their exact pinned Rust source."""
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
SOURCE_SHA256 = "de4c1e74bdf560cd44503ac9aa0a0c28b67cf8fa3f46e1330f4697e5c32147bc"
CODEC_PIN = "70ac6da7cf149cea6398a3d8829dd5e48b485b96"
HELPERS = ("placement", "ocs_plane", "straight_curve", "placed_curve", "compose_placements")


def checked(command: list[str | Path], *, cwd: Path, timeout: float = 180) -> str:
    status, output, _ = run_process([str(item) for item in command], timeout=timeout, cwd=cwd)
    if status:
        raise RuntimeError(f"exit {status}: {' '.join(map(str, command))}\n{output}")
    return output


def pin(path: Path, expected: str) -> None:
    actual = checked(["git", "-c", f"safe.directory={path.as_posix()}", "-C", path, "rev-parse", "HEAD"], cwd=ROOT).strip()
    if actual != expected:
        raise RuntimeError(f"source revision mismatch at {path}: {actual} != {expected}")


def extract(source: str, name: str) -> str:
    start = source.index(f"fn {name}(")
    opening = source.index("{", start)
    depth = 0
    for cursor in range(opening, len(source)):
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
            if depth == 0:
                return source[start:cursor + 1]
    raise RuntimeError(f"unterminated source helper: {name}")


def matrix(x=(1.0, 0.0, 0.0), y=(0.0, 1.0, 0.0), z=(0.0, 0.0, 1.0), origin=(0.0, 0.0, 0.0), column=(0.0, 0.0, 0.0, 1.0)) -> list[str]:
    values = [*x, column[0], *y, column[1], *z, column[2], *origin, column[3]]
    return [str(value) for value in values]


IDENTITY = matrix()
TURN = matrix((0, 1, 0), (-1, 0, 0), (0, 0, 1), (5, -2, 3))
SCALE = matrix((2, 0, 0), (0, 2, 0), (0, 0, 2), (1, 2, 3))
REFLECT = matrix((-1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 2, 3))
SHEAR = matrix((1, 0, 0), (0.25, 1, 0), (0, 0, 1))
ZERO = matrix((0, 0, 0), (0, 1, 0), (0, 0, 1))
BAD_COLUMN = matrix(column=(2e-9, 0, 0, 1))
BAD_COLUMN_7 = matrix(column=(0, -2e-9, 0, 1))
BAD_COLUMN_11 = matrix(column=(0, 0, 2e-9, 1))
BAD_LAST = matrix(column=(0, 0, 0, 1 + 2e-9))
NONUNIFORM = matrix((1, 0, 0), (0, 2, 0), (0, 0, 1))
NAN = matrix(origin=("nan", 0, 0))
INF = matrix(origin=("inf", 0, 0))

CASES: list[tuple[str, list[str], bool]] = [
    ("placement_identity", ["placement", *IDENTITY], True),
    ("placement_turn", ["placement", *TURN], True),
    ("placement_scale", ["placement", *SCALE], True),
    ("placement_reflection", ["placement", *REFLECT], True),
    ("placement_shear_is_only_parsed", ["placement", *SHEAR], True),
    ("placement_small_affine_residue", ["placement", *matrix(column=(5e-10, -5e-10, 5e-10, 1))], True),
    ("placement_bad_column", ["placement", *BAD_COLUMN], False),
    ("placement_bad_column_7", ["placement", *BAD_COLUMN_7], False),
    ("placement_bad_column_11", ["placement", *BAD_COLUMN_11], False),
    ("placement_bad_last", ["placement", *BAD_LAST], False),
    ("placement_nan", ["placement", *NAN], False),
    ("placement_infinite", ["placement", *INF], False),
    ("placement_zero_scale_is_only_parsed", ["placement", *ZERO], True),
    ("compose_turn_scale", ["compose", *TURN, *SCALE], True),
    ("compose_reflection_turn", ["compose", *REFLECT, *TURN], True),
    ("ocs_z", ["ocs", "0", "0", "1", "4"], True),
    ("ocs_negative_z", ["ocs", "0", "0", "-2", "4"], True),
    ("ocs_x", ["ocs", "1", "0", "0", "4"], True),
    ("ocs_near_pole", ["ocs", "0.0078125", "0", "1", "4"], True),
    ("ocs_away_from_pole", ["ocs", "0.03125", "0", "1", "4"], True),
    ("ocs_oblique", ["ocs", "2", "3", "4", "-5"], True),
    ("ocs_zero", ["ocs", "0", "0", "0", "1"], False),
    ("ocs_nan", ["ocs", "nan", "0", "1", "1"], False),
    ("straight_horizontal", ["straight", "1", "2", "3", "4", "6", "3"], True),
    ("straight_tilted", ["straight", "1", "2", "3", "4", "6", "9"], True),
    ("straight_vertical", ["straight", "1", "2", "3", "1", "2", "9"], True),
    ("straight_near_horizontal", ["straight", "1", "2", "3", "4", "6", "3.0000000005"], True),
    ("straight_short", ["straight", "1", "2", "3", "1", "2", "3.0000000000005"], False),
    ("straight_zero", ["straight", "1", "2", "3", "1", "2", "3"], False),
    ("straight_nan", ["straight", "nan", "2", "3", "4", "6", "3"], True),
    ("placed_identity", ["placed", *IDENTITY], True),
    ("placed_turn", ["placed", *TURN], True),
    ("placed_scale", ["placed", *SCALE], True),
    ("placed_reflection", ["placed", *REFLECT], True),
    ("placed_shear", ["placed", *SHEAR], False),
    ("placed_nonuniform", ["placed", *NONUNIFORM], False),
    ("placed_zero", ["placed", *ZERO], False),
    ("placed_bad_column", ["placed", *BAD_COLUMN], False),
    ("placed_bad_last", ["placed", *BAD_LAST], False),
]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--codec", required=True, type=Path)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    args = parser.parse_args()
    source, codec, compiler = (path.resolve() for path in (args.source, args.codec, args.compiler))
    pin(source, SOURCE_PIN)
    pin(codec, CODEC_PIN)
    source_bytes = (source / "src/acis/history.rs").read_bytes()
    actual_hash = hashlib.sha256(source_bytes).hexdigest()
    if actual_hash != SOURCE_SHA256:
        raise RuntimeError(f"history.rs hash mismatch: {actual_hash}")
    source_text = source_bytes.decode("utf-8")
    if shutil.which("cargo") is None:
        raise RuntimeError("cargo is required")

    parent = ROOT / "build/cadkernel-acis-history-frames-checks"
    parent.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    project = artifacts / "reference"
    (project / "src").mkdir(parents=True)
    (project / "src/main.rs").write_bytes((HERE / "reference.rs").read_bytes())
    (project / "src/history_helpers.rs").write_text("\n\n".join(extract(source_text, name) for name in HELPERS), encoding="utf-8")
    (project / "Cargo.toml").write_text(
        '[package]\nname = "cadkernel_acis_history_frames_reference"\nversion = "0.1.0"\nedition = "2021"\n'
        f'\n[dependencies]\ncadkernel = {{ path = "{source.as_posix()}", features = ["acis"] }}\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n'
        '\n[patch."https://github.com/HakanSeven12/cadcodec.git"]\n'
        f'acadrust = {{ path = "{codec.as_posix()}" }}\n',
        encoding="utf-8",
    )
    target = parent / "target"
    cargo = ["cargo"]
    if sys.platform == "win32":
        cargo.append("+stable-x86_64-pc-windows-msvc")
    checked([*cargo, "build", "--offline", "--manifest-path", project / "Cargo.toml", "--target-dir", target], cwd=ROOT, timeout=600)
    reference = target / "debug/cadkernel_acis_history_frames_reference.exe"
    if not reference.is_file():
        reference = reference.with_suffix("")

    fixture = ROOT / "tests/required/cadkernel_acis_history_frames"
    for mode in ("O0", "O2"):
        print(verify_fixture(fixture, mode, compiler, artifacts, 120, 20, False), flush=True)
        native = artifacts / f"differential-{mode}.out"
        checked([compiler, HERE / "differential.dl", f"-{mode}", "-o", native], cwd=ROOT, timeout=180)
        for label, parameters, valid in CASES:
            expected = normalize_output(checked([reference, *parameters], cwd=ROOT, timeout=20))
            if (not expected.startswith("invalid-")) != valid:
                raise RuntimeError(f"oracle status mismatch for {label}: {expected}")
            actual = normalize_output(checked([native, *parameters], cwd=ROOT, timeout=20))
            compare(f"{label} {mode}", expected, actual)
        print(f"{mode}: {len(CASES)} exact source-helper traces match", flush=True)
    print(f"Source: {SOURCE_PIN}; history.rs SHA256: {SOURCE_SHA256}")
    print(f"Codec: {CODEC_PIN}; compiler SHA256: {hashlib.sha256(compiler.read_bytes()).hexdigest()}")
    print(f"Artifacts: {artifacts}")


if __name__ == "__main__":
    main()
