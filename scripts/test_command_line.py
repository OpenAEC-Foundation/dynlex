#!/usr/bin/env python3
"""Integration tests for command-line DynLex source execution."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


if len(sys.argv) != 2:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <compiler>")

COMPILER = Path(sys.argv.pop()).resolve()


class CommandLineSourceTests(unittest.TestCase):
    def run_dynlex(self, *arguments: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(COMPILER), *arguments],
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=120,
        )

    def test_split_arguments_are_executed_as_source(self) -> None:
        result = self.run_dynlex("print", "3", "as", "line")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "3\n")

    def test_single_argument_is_executed_as_source(self) -> None:
        result = self.run_dynlex("print 4 as line")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "4\n")

    def test_double_dash_forces_source_mode(self) -> None:
        result = self.run_dynlex("--", "print", "5", "as", "line")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "5\n")

    def test_multiline_source_is_executed_without_rewriting(self) -> None:
        result = self.run_dynlex(
            "function double value:\n"
            "    execute:\n"
            "        return value + value\n"
            "print double 4 as line\n"
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "8\n")

    def test_command_wrapper_uses_project_import_syntax(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-command-syntax-") as temporary_directory:
            root = Path(temporary_directory)
            (root / "config.dl").write_text(
                'dynlex options:\n    import: "importeer"\n',
                encoding="utf-8",
            )

            result = self.run_dynlex("print", "6", "as", "line", cwd=root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "6\n")

    def test_command_prelude_provides_file_move(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-command-move-") as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source.txt"
            destination = root / "destination.txt"
            source.write_text("moved by DynLex\n", encoding="utf-8")

            result = self.run_dynlex(
                "move",
                '"source.txt"',
                "to",
                '"destination.txt"',
                cwd=root,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(source.exists())
            self.assertEqual(destination.read_text(encoding="utf-8"), "moved by DynLex\n")

    def test_executed_program_failure_is_returned(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-command-move-failure-") as temporary_directory:
            result = self.run_dynlex(
                "move",
                '"missing.txt"',
                "to",
                '"destination.txt"',
                cwd=Path(temporary_directory),
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing.txt", result.stderr)

    def test_diagnostics_point_to_command_line_source(self) -> None:
        result = self.run_dynlex("this pattern cannot resolve")

        self.assertEqual(result.returncode, 1)
        self.assertIn("<command-line>:1:", result.stderr)
        self.assertNotIn("<command-wrapper>", result.stderr)


if __name__ == "__main__":
    unittest.main()
