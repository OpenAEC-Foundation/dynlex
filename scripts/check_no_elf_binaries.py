#!/usr/bin/env python3
"""Reject ELF build artifacts in tracked files, regardless of name or mode."""

import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(os.fsdecode(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"]
    )).rstrip("\n"))
    paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
    binaries = []
    for raw_path in paths.split(b"\0"):
        if not raw_path:
            continue
        path = root / os.fsdecode(raw_path)
        # Deleted files and submodule directories have no file content to check.
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as source:
            if source.read(4) == b"\x7fELF":
                binaries.append(os.fsdecode(raw_path))
    if binaries:
        print("ELF build artifacts must not be committed:", file=sys.stderr)
        for path in binaries:
            print(f"  {path}", file=sys.stderr)
        return 1
    print("No tracked ELF binaries found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
