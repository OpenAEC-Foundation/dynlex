#!/usr/bin/env python3
"""Validate bounded compile-time budgets for required integration fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CompileBudgetTests(unittest.TestCase):
    def invoke(self, directory, default="20"):
        return subprocess.run(
            [os.environ.get("DYNLEX_TEST_BASH", "bash"), "-c",
             'source scripts/test_fixture_options.sh && dynlex_fixture_compile_timeout "$1" "$2"',
             "fixture-budget", str(directory), default],
            cwd=ROOT, text=True, capture_output=True, timeout=10)

    def test_default_and_bounded_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            for default in ("10", "20"):
                result=self.invoke(directory,default)
                self.assertEqual((result.returncode,result.stdout.strip()),(0,default))
            for text,expected in (("1\n","1"),("60\n","60"),("300\r\n","300")):
                (directory/"compile_timeout_seconds.txt").write_bytes(text.encode())
                result=self.invoke(directory)
                self.assertEqual((result.returncode,result.stdout.strip()),(0,expected))

    def test_rejects_invalid_limits(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            for text in ("", "0", "-1", "301", "99999999999999999", "1.5", " 60", "01", "abc", "60\n20"):
                with self.subTest(text=text):
                    (directory/"compile_timeout_seconds.txt").write_text(text)
                    result=self.invoke(directory)
                    self.assertNotEqual(result.returncode,0)
                    self.assertEqual(result.stdout,"")
                    self.assertIn("compile_timeout_seconds.txt",result.stderr)


if __name__=="__main__":
    unittest.main()
