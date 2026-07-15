#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path

from diagnostic_expectations import normalize_diagnostics


class DiagnosticExpectationsTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
