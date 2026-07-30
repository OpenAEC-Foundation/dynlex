#!/usr/bin/env python3

import os
from pathlib import Path
import subprocess
import sys


if len(sys.argv) != 2:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <project-directory>")

project = Path(sys.argv[1]).resolve()
build = project / "build"
subprocess.run(
    ["cmake", "--build", str(build), "--target", "dynlex_class_layout_test"],
    cwd=project,
    check=True,
)
executable = build / ("dynlex_class_layout_test.exe" if os.name == "nt" else "dynlex_class_layout_test")
subprocess.run([str(executable)], cwd=project, check=True)
