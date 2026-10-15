# SPDX-License-Identifier: MPL-2.0
"""Native Deviation2 source fixtures and differential verification."""
from pathlib import Path
import runpy
import sys

sys.dont_write_bytecode = True
if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parents[1] / "arclength2/verify.py"))["main"]("deviation")
