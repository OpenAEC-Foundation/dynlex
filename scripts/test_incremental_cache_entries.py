#!/usr/bin/env python3
"""Verify the file ownership used by lazy incremental program snapshots."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

project = Path(sys.argv[1]).resolve()
executable = project / "build" / ("dynlex_incremental_cache_entry_test.exe" if os.name == "nt" else "dynlex_incremental_cache_entry_test")
with tempfile.TemporaryDirectory(prefix="dynlex-cache-entry-") as directory:
    subprocess.run([str(executable), directory], cwd=project, check=True)
