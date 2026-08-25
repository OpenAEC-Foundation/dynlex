#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


if len(sys.argv) != 2:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <project-directory>")

project = Path(sys.argv[1]).resolve()
build = project / "build"
subprocess.run(
    ["cmake", "--build", str(build), "--target", "dynlex_native_target_test"],
    check=True,
)
executable = build / ("dynlex_native_target_test.exe" if sys.platform == "win32" else "dynlex_native_target_test")
subprocess.run([str(executable)], check=True)
