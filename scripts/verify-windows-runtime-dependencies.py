#!/usr/bin/env python3
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path


FORBIDDEN_RUNTIME = re.compile(
    r"^(?:atl|concrt|mfc|msvcp|msvcr|vcamp|vccorlib|vcomp|vcruntime)[^.]*\.dll$",
    re.IGNORECASE,
)


def unpack_from(fmt: str, contents: bytes, offset: int, description: str) -> tuple[int, ...]:
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(contents):
        raise ValueError(f"truncated {description}")
    return struct.unpack_from(fmt, contents, offset)


def read_c_string(contents: bytes, offset: int) -> str:
    end = contents.find(b"\0", offset)
    if offset < 0 or end < 0:
        raise ValueError("unterminated PE import name")
    try:
        return contents[offset:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("non-ASCII PE import name") from error


def imported_libraries(contents: bytes) -> list[str]:
    if len(contents) < 64 or contents[:2] != b"MZ":
        raise ValueError("file is not a PE image")
    (pe_offset,) = unpack_from("<I", contents, 0x3C, "DOS header")
    if contents[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")

    coff_offset = pe_offset + 4
    _, section_count, _, _, _, optional_size, _ = unpack_from(
        "<HHIIIHH",
        contents,
        coff_offset,
        "COFF header",
    )
    optional_offset = coff_offset + 20
    (optional_magic,) = unpack_from(
        "<H",
        contents,
        optional_offset,
        "optional header",
    )
    if optional_magic == 0x20B:
        directory_count_offset = optional_offset + 108
        directories_offset = optional_offset + 112
    elif optional_magic == 0x10B:
        directory_count_offset = optional_offset + 92
        directories_offset = optional_offset + 96
    else:
        raise ValueError(f"unsupported PE optional-header magic: {optional_magic:#x}")
    (directory_count,) = unpack_from(
        "<I",
        contents,
        directory_count_offset,
        "data-directory count",
    )
    if directory_count < 2:
        return []
    import_rva, _ = unpack_from(
        "<II",
        contents,
        directories_offset + 8,
        "import data directory",
    )
    if import_rva == 0:
        return []

    sections: list[tuple[int, int, int, int]] = []
    section_offset = optional_offset + optional_size
    for index in range(section_count):
        header_offset = section_offset + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = unpack_from(
            "<IIII",
            contents,
            header_offset + 8,
            "section header",
        )
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, virtual_size, raw_offset, raw_size in sections:
            mapped_size = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + mapped_size:
                result = raw_offset + rva - virtual_address
                if result >= len(contents):
                    break
                return result
        raise ValueError(f"PE RVA {rva:#x} is outside mapped sections")

    descriptor_offset = rva_to_offset(import_rva)
    imports: list[str] = []
    while True:
        descriptor = unpack_from(
            "<IIIII",
            contents,
            descriptor_offset,
            "import descriptor",
        )
        if descriptor == (0, 0, 0, 0, 0):
            break
        imports.append(read_c_string(contents, rva_to_offset(descriptor[3])))
        descriptor_offset += 20
    return imports


def verify_file(path: Path) -> None:
    imports = imported_libraries(path.read_bytes())
    forbidden = sorted(name for name in imports if FORBIDDEN_RUNTIME.fullmatch(name))
    if forbidden:
        raise ValueError(
            "dynamically imports the Visual C++ runtime: " + ", ".join(forbidden),
        )
    print(f"Verified {path} has no dynamic Visual C++ runtime dependency.")


def main() -> int:
    if len(sys.argv) < 2:
        print(
            f"Usage: {Path(sys.argv[0]).name} <pe-file>...",
            file=sys.stderr,
        )
        return 2
    for argument in sys.argv[1:]:
        path = Path(argument)
        if not path.is_file():
            print(f"PE file does not exist: {path}", file=sys.stderr)
            return 1
        try:
            verify_file(path)
        except ValueError as error:
            print(f"Could not verify {path}: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
