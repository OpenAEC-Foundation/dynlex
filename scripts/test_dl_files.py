#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from dl_files import discover_dl_files


class DiscoverDynLexFilesTests(unittest.TestCase):
    def test_repository_worktrees_are_excluded(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-discovery-") as temporary_directory:
            repo_root = Path(temporary_directory)
            source = repo_root / "main.dl"
            worktree_source = repo_root / ".worktrees" / "feature" / "main.dl"
            source.write_text("print 1 as line\n", encoding="utf-8")
            worktree_source.parent.mkdir(parents=True)
            worktree_source.write_text("print 2 as line\n", encoding="utf-8")

            self.assertEqual(discover_dl_files(repo_root), [source.resolve()])


if __name__ == "__main__":
    unittest.main()
