#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare open two-section B-rep topology with the pinned public Rust loft."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
import verify as fixtures

PIN = "953d546b68aef4b6692566a1a9b077fc5bd9fb4f"
SOURCE_SHA = "e0e38b79eb944f6b4e5a2fa263c7deb6b12e4ff2d2da238d327720fbdaf4fd98"


def run(command: list[str | Path], *, timeout: float = 240) -> str:
    code, output, _ = fixtures.run_process([str(item) for item in command], timeout=timeout, cwd=ROOT)
    if code:
        raise AssertionError(f"exit {code}: {command}\n{output}")
    return output.strip()


def line(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, ...]:
    return (0., *a, *b)


def arc(centre: tuple[float, float], radius: float, start: float, end: float) -> tuple[float, ...]:
    return (2., *centre, radius, start, end)


def frame(z: float, x: float = 0., y: float = 0., angle: float = 0.,
          shear: float = 0.) -> tuple[float, ...]:
    c, s = math.cos(angle), math.sin(angle)
    return (x, y, z, c, s, 0., -s + shear, c, 0.)


@dataclass(frozen=True)
class Section:
    plane: tuple[float, ...]
    wires: tuple[tuple[tuple[float, ...], ...], ...]
    closed: bool = False


@dataclass(frozen=True)
class Case:
    name: str
    sections: tuple[Section, ...]
    matching: bool = True
    mode: int = 1
    start_angle: float = math.pi / 2
    end_angle: float = math.pi / 2
    start_magnitude: float = 0.
    end_magnitude: float = 0.
    expected_valid: bool = True

    def arguments(self) -> list[str]:
        values: list[float | int] = [int(self.matching), self.mode, self.start_angle,
                                     self.end_angle, self.start_magnitude, self.end_magnitude,
                                     len(self.sections)]
        for section in self.sections:
            values.extend(section.plane)
            values.extend((int(section.closed), len(section.wires)))
            for wire in section.wires:
                values.append(len(wire))
                for piece in wire:
                    values.extend(piece)
        return [format(float(value), ".17g") for value in values]


def profile(z: float, segments: tuple[tuple[float, ...], ...], **placement: float) -> Section:
    return Section(frame(z, **placement), (segments,))


def cases() -> list[Case]:
    one = (line((0., 0.), (2., 0.)),)
    two = (line((0., 0.), (1., 0.)), line((1., 0.), (2., 0.)))
    bent = (line((0., 0.), (1., 0.)), line((1., 0.), (2., 1.)))
    reverse = (line((2., 0.), (0., 0.)),)
    curve = (arc((0., 0.), 2., 0., math.pi / 2),)
    broken = (line((0., 0.), (1., 0.)), line((1.2, 0.), (2., 0.)))
    collapsed = (line((0., 0.), (0., 0.)),)
    collapsed_two = (line((0., 0.), (0., 0.)), line((0., 0.), (0., 0.)))
    origin = profile(0., one)
    return [
        Case("one-ruled", (origin, profile(3., one)), mode=0),
        Case("one-smooth", (origin, profile(3., one))),
        Case("one-translated", (origin, profile(3., one, x=1., y=.5))),
        Case("two-straight", (profile(0., two), profile(3., two))),
        Case("two-bent", (profile(0., bent), profile(3., bent, x=.5))),
        Case("unequal-segments", (origin, profile(3., two))),
        Case("reverse-aligned", (origin, profile(3., reverse))),
        Case("reverse-preserved", (origin, profile(3., reverse)), matching=False, expected_valid=False),
        Case("arc-pair", (profile(0., curve), profile(3., curve))),
        Case("arc-line", (profile(0., curve), profile(3., one))),
        Case("rotated-frame", (profile(0., bent), profile(3., bent, angle=.25))),
        Case("sheared-frame", (profile(0., two), profile(3., two, shear=.2))),
        Case("first-normal", (origin, profile(3., one)), mode=2),
        Case("both-normal", (origin, profile(3., one)), mode=4),
        Case("draft", (origin, profile(3., one)), mode=6,
             start_angle=.9, end_angle=1.2, start_magnitude=1.5, end_magnitude=.75),
        Case("collapse-last", (origin, profile(3., collapsed))),
        Case("collapse-first", (profile(0., collapsed), profile(3., one))),
        Case("two-collapse-last", (profile(0., two), profile(3., collapsed_two))),
        Case("two-collapse-first", (profile(0., collapsed_two), profile(3., two))),
        Case("disconnected", (profile(0., broken), profile(3., broken)), expected_valid=False),
        Case("empty-wire", (profile(0., ()), profile(3., one)), expected_valid=False),
        Case("coincident", (origin, profile(0., one)), expected_valid=False),
        Case("bad-plane", (origin, Section((0., 0., 3., 0., 0., 0., 0., 1., 0.), (one,))), expected_valid=False),
        Case("ray", (profile(0., ((5., 0., 0., 1., 0.),)), profile(3., one)), expected_valid=False),
        Case("one-section", (origin,), expected_valid=False),
    ]


def compare(name: str, rust: str, native: str, expected: bool) -> int:
    left = [float(value) for value in rust.splitlines()]
    right = [float(value) for value in native.splitlines()]
    if not left or left[0] != float(expected):
        raise AssertionError(f"{name}: pinned Rust validity {left[:1]} differs from expectation {expected}")
    if len(left) != len(right):
        raise AssertionError(f"{name}: field counts {len(left)} != {len(right)}\nRust:\n{rust}\nDynLex:\n{native}")
    for index, (a, b) in enumerate(zip(left, right)):
        if not math.isclose(a, b, rel_tol=2e-9, abs_tol=2e-8):
            raise AssertionError(f"{name}: field {index} differs: Rust={a:.17g}, DynLex={b:.17g}")
    return len(left)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/dynlex.exe")
    parser.add_argument("--reference-root", type=Path,
                        help="prebuilt pinned O0/O2 Rust target; otherwise build from the pinned checkout")
    parser.add_argument("--case", default="")
    args = parser.parse_args()
    source = args.source.resolve()
    revision = run(["git", "-c", f"safe.directory={source}", "-C", source, "rev-parse", "HEAD"])
    if revision != PIN:
        raise AssertionError(f"Rust source revision {revision} != {PIN}")
    digest = hashlib.sha256((source / "src/brep/loft_general.rs").read_bytes()).hexdigest()
    if digest != SOURCE_SHA:
        raise AssertionError(f"Rust source hash {digest} != {SOURCE_SHA}")
    suffix = ".exe" if sys.platform == "win32" else ".out"
    tested = fields = 0
    # A Windows process can briefly retain an incremental build file handle.
    with tempfile.TemporaryDirectory(prefix="cad-loft-general-body-open-", dir=ROOT / "build",
                                     ignore_cleanup_errors=True) as directory:
        temporary = Path(directory)
        if args.reference_root is None:
            copied = temporary / "source"
            shutil.copytree(source, copied)
            reference_root = temporary / "reference"
            for release in (False, True):
                command = ["cargo", "build", "--offline", "--manifest-path", copied / "Cargo.toml",
                           "--target-dir", reference_root, "--package", "cadkernel", "--features", "brep"]
                if release:
                    command.append("--release")
                run(command, timeout=600)
        else:
            reference_root = args.reference_root.resolve()
        for mode, folder in (("O0", "debug"), ("O2", "release")):
            library = reference_root / folder / "libcadkernel.rlib"
            dependencies = reference_root / folder / "deps"
            if not library.is_file() or not dependencies.is_dir():
                raise AssertionError(f"missing pinned Rust library {library}")
            rust_binary = temporary / f"reference-{mode}{suffix}"
            native_binary = temporary / f"native-{mode}{suffix}"
            run(["rustc", "--edition=2021", HERE / "reference.rs", "--extern",
                 f"cadkernel={library}", "-L", f"dependency={dependencies}",
                 "-C", f"opt-level={mode[1]}", "-o", rust_binary])
            run([args.compiler.resolve(), HERE / "probe.dl", f"-{mode}", "-o", native_binary], timeout=180)
            for item in cases():
                if args.case and args.case not in item.name:
                    continue
                values = item.arguments()
                rust = run([rust_binary, *values], timeout=20)
                native = run([native_binary, *values], timeout=20)
                fields += compare(f"{item.name}/{mode}", rust, native, item.expected_valid)
                tested += 1
            if not args.case:
                fixture = ROOT / "tests/required/cadkernel_brep_loft_general_body_open"
                print(f"fixture {mode}: {fixtures.verify_fixture(fixture, mode, args.compiler.resolve(), temporary, 180, 60, False)}", flush=True)
    if tested == 0:
        raise AssertionError("no cases selected")
    print(f"PASS: {tested // 2} cases O0/O2, {fields} compared fields; source={revision[:7]}", flush=True)


if __name__ == "__main__":
    main()
