#!/usr/bin/env python3

import struct
import sys
from pathlib import Path


ARCHITECTURES = {
    "ELF": {
        62: "x64",
        183: "arm64",
    },
    "PE": {
        0x8664: "x64",
        0xAA64: "arm64",
    },
}


def read_elf_architecture(contents: bytes) -> str | None:
    if len(contents) < 20 or contents[:4] != b"\x7fELF":
        return None
    byte_order = contents[5]
    if byte_order == 1:
        machine = struct.unpack_from("<H", contents, 18)[0]
    elif byte_order == 2:
        machine = struct.unpack_from(">H", contents, 18)[0]
    else:
        raise ValueError("ELF executable has an invalid byte order")
    try:
        return ARCHITECTURES["ELF"][machine]
    except KeyError as error:
        raise ValueError(f"unsupported ELF machine type: {machine}") from error


def read_pe_architecture(contents: bytes) -> str | None:
    if len(contents) < 64 or contents[:2] != b"MZ":
        return None
    pe_offset = struct.unpack_from("<I", contents, 0x3C)[0]
    if pe_offset + 6 > len(contents) or contents[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("PE executable has an invalid header")
    machine = struct.unpack_from("<H", contents, pe_offset + 4)[0]
    try:
        return ARCHITECTURES["PE"][machine]
    except KeyError as error:
        raise ValueError(f"unsupported PE machine type: {machine:#x}") from error


def executable_architecture(contents: bytes) -> str:
    for reader in (read_elf_architecture, read_pe_architecture):
        architecture = reader(contents)
        if architecture is not None:
            return architecture
    raise ValueError("file is not a supported ELF or PE executable")


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"x64", "arm64"}:
        print(
            f"Usage: {Path(sys.argv[0]).name} <executable> <x64|arm64>",
            file=sys.stderr,
        )
        return 2

    executable_path = Path(sys.argv[1])
    if not executable_path.is_file():
        print(f"Executable does not exist: {executable_path}", file=sys.stderr)
        return 1

    try:
        actual_architecture = executable_architecture(executable_path.read_bytes())
    except ValueError as error:
        print(f"Could not verify {executable_path}: {error}", file=sys.stderr)
        return 1

    expected_architecture = sys.argv[2]
    if actual_architecture != expected_architecture:
        print(
            f"{executable_path} is {actual_architecture}, expected {expected_architecture}.",
            file=sys.stderr,
        )
        return 1

    print(f"Verified {executable_path} as {actual_architecture}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
