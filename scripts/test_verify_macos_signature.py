#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
VERIFIER = SCRIPT_DIR / "verify-macos-signature.sh"
APPLICATION_IDENTITY = "Developer ID Application: Example (ABCDEFGHIJ)"
INSTALLER_IDENTITY = "Developer ID Installer: Example (ABCDEFGHIJ)"
TEAM_ID = "ABCDEFGHIJ"


def write_executable(path: Path, contents: str) -> None:
    path.write_text(contents, encoding="utf-8")
    path.chmod(0o755)


@unittest.skipIf(os.name == "nt", "macOS signature verifier uses POSIX mock tools")
class MacOSSignatureVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory(
            prefix="dynlex-macos-signature-test-",
        )
        self.root = Path(self.temporary_directory.name)
        self.bin_directory = self.root / "bin"
        self.bin_directory.mkdir()
        self.package = self.root / "dynlex.pkg"
        self.package.touch()
        self.binary = self.root / "dynlex"
        write_executable(self.binary, "#!/bin/bash\nexit 0\n")

        write_executable(
            self.bin_directory / "pkgutil",
            "#!/bin/bash\nprintf '%s\\n' \"$MOCK_INSTALLER_IDENTITY\"\n",
        )
        write_executable(self.bin_directory / "xcrun", "#!/bin/bash\nexit 0\n")
        write_executable(self.bin_directory / "spctl", "#!/bin/bash\nexit 0\n")
        write_executable(
            self.bin_directory / "codesign",
            """#!/bin/bash
if [[ "$1" == "--verify" ]]; then
    exit 0
fi
if [[ "$1" != "--display" ]]; then
    exit 2
fi
printf 'Authority=%s\n' "$MOCK_APPLICATION_IDENTITY" >&2
printf 'TeamIdentifier=%s\n' "$MOCK_TEAM_ID" >&2
printf 'CodeDirectory v=20500 flags=%s\n' "$MOCK_CODE_FLAGS" >&2
printf 'Timestamp=Jul 30, 2026 at 3:02:35 PM\n' >&2
""",
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_verifier(self, **overrides: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update({
            "APPLE_APPLICATION_SIGNING_IDENTITY": APPLICATION_IDENTITY,
            "APPLE_INSTALLER_SIGNING_IDENTITY": INSTALLER_IDENTITY,
            "APPLE_TEAM_ID": TEAM_ID,
            "MOCK_APPLICATION_IDENTITY": APPLICATION_IDENTITY,
            "MOCK_INSTALLER_IDENTITY": INSTALLER_IDENTITY,
            "MOCK_TEAM_ID": TEAM_ID,
            "MOCK_CODE_FLAGS": "0x10000(runtime)",
            "PATH": f"{self.bin_directory}:{environment['PATH']}",
        })
        environment.update(overrides)
        return subprocess.run(
            [str(VERIFIER), str(self.package), str(self.binary)],
            capture_output=True,
            env=environment,
            text=True,
            timeout=5,
        )

    def test_accepts_expected_notarized_package_and_binary(self) -> None:
        completed = self.run_verifier()
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_rejects_unexpected_installer_identity(self) -> None:
        completed = self.run_verifier(
            MOCK_INSTALLER_IDENTITY="Developer ID Installer: Other (ZZZZZZZZZZ)",
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            "not signed by the expected Apple Developer ID Installer identity",
            completed.stderr,
        )

    def test_rejects_binary_without_hardened_runtime(self) -> None:
        completed = self.run_verifier(MOCK_CODE_FLAGS="0x0(none)")
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("does not have the hardened runtime enabled", completed.stderr)


if __name__ == "__main__":
    unittest.main()
