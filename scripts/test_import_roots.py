#!/usr/bin/env python3
"""Import resolution must keep one import graph on one resolution root.

A file resolved from the compiler's library tree resolves its own imports
under that same tree, even when the working directory contains files with the
same relative paths. Without root inheritance, the standard library's nested
imports fall back to the working directory and mix trees.
"""
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
COMPILER = SCRIPT_DIR.parent / "build" / "dynlex"


class ImportRootConsistencyTests(unittest.TestCase):
    def test_nested_library_imports_ignore_working_directory(self) -> None:
        self.assertTrue(COMPILER.exists(), f"compiler not built at {COMPILER}")
        with tempfile.TemporaryDirectory(prefix="dynlex-import-roots-") as temporary_directory:
            root = Path(temporary_directory)
            main = root / "main.dl"
            main.write_text('import std.dl\nprint "ok" as line\n', encoding="utf-8")

            # A working directory whose lib/string.dl would break the build if
            # the standard library's nested `import lib/string.dl` fell back to
            # the working directory instead of staying on the resolved root.
            work = root / "work"
            (work / "lib").mkdir(parents=True)
            (work / "lib" / "string.dl").write_text("this is not valid dynlex $$$\n", encoding="utf-8")

            output = root / "main.out"
            compileResult = subprocess.run(
                [str(COMPILER), str(main), "-o", str(output)],
                cwd=work,
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertEqual(
                compileResult.returncode, 0,
                f"compile failed, likely mixed import roots:\n{compileResult.stdout}\n{compileResult.stderr}",
            )
            runResult = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
            self.assertEqual(runResult.returncode, 0)
            self.assertEqual(runResult.stdout.strip(), "ok")


if __name__ == "__main__":
    unittest.main()
