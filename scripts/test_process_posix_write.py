#!/usr/bin/env python3

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def compiler() -> str:
    for candidate in ("clang-20", "clang"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError("A Clang C compiler is required for POSIX process write tests")


def main() -> int:
    if os.name == "nt":
        return 0
    project = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-process-posix-write-test-") as temporary:
        executable = Path(temporary) / "test"
        subprocess.run(
            [
                compiler(),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Werror",
                f"-I{project / 'src/runtime'}",
                str(project / "tests/runtime/process_runtime_posix_write.c"),
                str(project / "src/runtime/processRuntimePosixWrite.c"),
                "-pthread",
                "-o",
                str(executable),
            ],
            cwd=project,
            check=True,
        )
        subprocess.run([executable], cwd=project, check=True, timeout=5)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
