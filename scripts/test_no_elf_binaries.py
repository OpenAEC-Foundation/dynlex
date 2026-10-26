#!/usr/bin/env python3
"""Exercise the repository ELF guard in isolated Git repositories."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECKER = Path(__file__).with_name("check_no_elf_binaries.py").resolve()


class ElfGuardTests(unittest.TestCase):
    def test_tracked_content_and_ignored_build_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            (root / ".gitignore").write_text("build/\n")
            (root / "source.py").write_text("#!/usr/bin/env python3\n")
            (root / "source.py").chmod(0o755)
            (root / "build").mkdir()
            (root / "build/program").write_bytes(b"\x7fELFignored")
            subprocess.run(["git", "add", "."], cwd=root, check=True)

            def check():
                return subprocess.run(
                    [sys.executable, str(CHECKER)], cwd=root,
                    capture_output=True, text=True,
                )

            self.assertEqual(check().returncode, 0)
            # A forced addition with an arbitrary name and no executable bit
            # must still be rejected by content.
            binary = root / "build/unexpected name.dat"
            binary.write_bytes(b"\x7fELFartifact")
            subprocess.run(["git", "add", "-f", str(binary)], cwd=root, check=True)
            result = check()
            self.assertEqual(result.returncode, 1)
            self.assertIn("build/unexpected name.dat", result.stderr)
            binary.unlink()
            self.assertEqual(check().returncode, 0)


if __name__ == "__main__":
    unittest.main()
