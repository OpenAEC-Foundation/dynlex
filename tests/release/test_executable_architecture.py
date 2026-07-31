#!/usr/bin/env python3

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_DIRECTORY = Path(__file__).resolve().parents[2]
VERIFIER = PROJECT_DIRECTORY / "scripts" / "verify-executable-architecture.py"


def elf_header(machine: int) -> bytes:
    header = bytearray(64)
    header[:4] = b"\x7fELF"
    header[4] = 2
    header[5] = 1
    header[18:20] = machine.to_bytes(2, "little")
    return bytes(header)


def pe_header(machine: int) -> bytes:
    header = bytearray(256)
    header[:2] = b"MZ"
    header[0x3C:0x40] = (128).to_bytes(4, "little")
    header[128:132] = b"PE\0\0"
    header[132:134] = machine.to_bytes(2, "little")
    return bytes(header)


class ExecutableArchitectureVerifierTests(unittest.TestCase):
    def assert_verification(
        self,
        contents: bytes,
        expected_architecture: str,
        expected_success: bool,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-architecture-") as directory:
            executable = Path(directory) / "binary"
            executable.write_bytes(contents)
            completed = subprocess.run(
                [sys.executable, str(VERIFIER), str(executable), expected_architecture],
                capture_output=True,
                text=True,
            )
            if expected_success:
                self.assertEqual(completed.returncode, 0, completed.stderr)
            else:
                self.assertNotEqual(completed.returncode, 0)

    def test_elf_architectures(self) -> None:
        self.assert_verification(elf_header(62), "x64", True)
        self.assert_verification(elf_header(183), "arm64", True)
        self.assert_verification(elf_header(62), "arm64", False)

    def test_pe_architectures(self) -> None:
        self.assert_verification(pe_header(0x8664), "x64", True)
        self.assert_verification(pe_header(0xAA64), "arm64", True)
        self.assert_verification(pe_header(0xAA64), "x64", False)

    def test_unknown_format_fails(self) -> None:
        self.assert_verification(b"not an executable", "x64", False)


if __name__ == "__main__":
    unittest.main()
