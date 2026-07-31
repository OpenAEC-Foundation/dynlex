#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import struct
import unittest
from pathlib import Path


SCRIPT = (
    Path(__file__).resolve().parents[2]
    / "scripts"
    / "verify-windows-runtime-dependencies.py"
)
SPEC = importlib.util.spec_from_file_location("runtime_dependencies", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def pe_with_imports(imports: list[str]) -> bytes:
    pe_offset = 0x80
    optional_size = 240
    section_offset = pe_offset + 4 + 20 + optional_size
    raw_offset = 0x200
    section_rva = 0x1000
    descriptor_size = 20 * (len(imports) + 1)
    names = bytearray()
    descriptors = bytearray()
    for name in imports:
        name_rva = section_rva + descriptor_size + len(names)
        descriptors.extend(struct.pack("<IIIII", 0, 0, 0, name_rva, 0))
        names.extend(name.encode("ascii") + b"\0")
    descriptors.extend(bytes(20))
    section_contents = descriptors + names

    contents = bytearray(raw_offset + len(section_contents))
    contents[:2] = b"MZ"
    struct.pack_into("<I", contents, 0x3C, pe_offset)
    contents[pe_offset:pe_offset + 4] = b"PE\0\0"
    coff_offset = pe_offset + 4
    struct.pack_into(
        "<HHIIIHH",
        contents,
        coff_offset,
        0x8664,
        1,
        0,
        0,
        0,
        optional_size,
        0x2022,
    )
    optional_offset = coff_offset + 20
    struct.pack_into("<H", contents, optional_offset, 0x20B)
    struct.pack_into("<I", contents, optional_offset + 108, 16)
    struct.pack_into(
        "<II",
        contents,
        optional_offset + 112 + 8,
        section_rva,
        descriptor_size,
    )
    contents[section_offset:section_offset + 8] = b".rdata\0\0"
    struct.pack_into(
        "<IIII",
        contents,
        section_offset + 8,
        len(section_contents),
        section_rva,
        len(section_contents),
        raw_offset,
    )
    contents[raw_offset:] = section_contents
    return bytes(contents)


class WindowsRuntimeDependencyTests(unittest.TestCase):
    def test_reads_pe_import_table(self) -> None:
        self.assertEqual(
            MODULE.imported_libraries(
                pe_with_imports(["KERNEL32.dll", "glfw3.dll"]),
            ),
            ["KERNEL32.dll", "glfw3.dll"],
        )

    def test_rejects_every_visual_cpp_runtime_family(self) -> None:
        for name in (
            "atl110.dll",
            "concrt140.dll",
            "mfc140u.dll",
            "mfcm140u.dll",
            "msvcp140.dll",
            "msvcp140_atomic_wait.dll",
            "msvcp140_codecvt_ids.dll",
            "msvcr120.dll",
            "vcamp140.dll",
            "vccorlib140.dll",
            "vcomp140.dll",
            "vcruntime140.dll",
            "vcruntime140_1.dll",
        ):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, name):
                    MODULE.verify_file(self._write_fixture(name))

    def test_accepts_windows_ucrt_imports(self) -> None:
        fixture = self._write_fixture("api-ms-win-crt-runtime-l1-1-0.dll")
        MODULE.verify_file(fixture)

    def _write_fixture(self, import_name: str) -> Path:
        fixture = Path(self.id().replace(".", "-") + ".exe")
        self.addCleanup(fixture.unlink, missing_ok=True)
        fixture.write_bytes(pe_with_imports([import_name]))
        return fixture


if __name__ == "__main__":
    unittest.main()
