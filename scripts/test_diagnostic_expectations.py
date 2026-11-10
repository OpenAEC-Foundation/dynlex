#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path
import os
import subprocess

from diagnostic_expectations import normalize_diagnostics


class DiagnosticExpectationsTests(unittest.TestCase):
    def runner_normalize(self, name: str, text: str) -> subprocess.CompletedProcess:
        root = Path(__file__).resolve().parents[1]
        source = (root / "scripts" / "test.sh").read_text(encoding="utf-8")
        function = name + "() {" + source.split(name + "() {", 1)[1].split("\n}", 1)[0] + "\n}"
        return subprocess.run(
            [os.environ.get("DYNLEX_TEST_BASH", "bash"), "-c",
             'PROJECT_DIR=$1\nSCRIPT_DIR=$2\n' + function + '\n' + name + ' "$3"',
             "runner-normalization", root.as_posix(), (root / "scripts").as_posix(), text],
            capture_output=True, text=True, timeout=5,
        )

    def test_runner_normalizes_empty_and_nonempty_diagnostics(self) -> None:
        root = Path(__file__).resolve().parents[1]
        for text in ("", f"{root.as_posix()}/test.dl:12:4-19: Error: failure\r\n"):
            with self.subTest(text=text):
                result = self.runner_normalize("normalize_diagnostics", text)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, normalize_diagnostics(text, root))

    def test_runner_output_normalization_preserves_other_characters(self) -> None:
        for text in ("", "  café\r\n\n\t\rresult  \r\n"):
            with self.subTest(text=text):
                result = self.runner_normalize("normalize_output", text)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, text.replace("\r", ""))

    def test_source_line_numbers_are_removed_but_column_ranges_are_kept(self) -> None:
        project_dir = Path("/repo")
        diagnostics = (
            "/repo/tests/example/main.dl:12:4-19: Error: failure\r\n"
            "  note: more context /repo/lib/std.dl:227:16-54  \r\n"
        )

        self.assertEqual(
            normalize_diagnostics(diagnostics, project_dir),
            "tests/example/main.dl:4-19: Error: failure\n"
            "  note: more context lib/std.dl:16-54",
        )

    def test_line_numbered_expectation_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            ValueError,
            "expected diagnostics must not contain source line numbers",
        ):
            normalize_diagnostics(
                "tests/example/main.dl:12:4-19: Error: failure\n",
                Path("/repo"),
                reject_line_numbers=True,
            )

    def test_line_independent_expectation_is_accepted(self) -> None:
        self.assertEqual(
            normalize_diagnostics(
                "tests/example/main.dl:4-19: Error: failure\n",
                Path("/repo"),
                reject_line_numbers=True,
            ),
            "tests/example/main.dl:4-19: Error: failure",
        )

    def test_windows_project_paths_are_normalized(self) -> None:
        self.assertEqual(
            normalize_diagnostics(
                "C:/repo/tests/example/main.dl:12:4-19: Error: failure\n",
                Path("/c/repo"),
            ),
            "tests/example/main.dl:4-19: Error: failure",
        )

    def test_native_windows_project_paths_are_normalized(self) -> None:
        self.assertEqual(
            normalize_diagnostics(
                "<unknown location>: Error: syntax config "
                "C:\\repo\\tests\\example\\config.dl:3: failure\n",
                Path("/c/repo"),
            ),
            "<unknown location>: Error: syntax config "
            "tests/example/config.dl:3: failure",
        )

    def test_project_paths_without_source_locations_are_relative(self) -> None:
        self.assertEqual(
            normalize_diagnostics(
                "<unknown location>: Error: output at /repo/build/result.txt\n",
                Path("/repo"),
            ),
            "<unknown location>: Error: output at build/result.txt",
        )


if __name__ == "__main__":
    unittest.main()
