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
    raise RuntimeError("A Clang C compiler is required for filesystem transaction runtime tests")


def main() -> int:
    if os.name == "nt":
        return 0
    project = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-filesystem-transaction-test-") as temporary:
        common = [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-DDYNLEX_FILESYSTEM_TESTING=1",
            f"-I{project / 'src/runtime'}",
            str(project / "tests/runtime/filesystem_transaction_posix.c"),
            str(project / "src/runtime/filesystemRuntime.c"),
            str(project / "src/runtime/filesystemRuntimePosix.c"),
            str(project / "src/runtime/filesystemTransactionPosix.c"),
            str(project / "src/runtime/runtimeError.c"),
            str(project / "src/runtime/runtimeText.c"),
        ]
        for name, sanitizer_flags in (
            ("test", []),
            (
                "test-sanitized",
                [
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                ],
            ),
        ):
            executable = Path(temporary) / name
            subprocess.run(
                [compiler(), *common, *sanitizer_flags, "-o", str(executable)],
                cwd=project,
                check=True,
            )
            subprocess.run([executable], cwd=project, check=True)

        windows_source = project / "src/runtime/filesystemTransactionWindows.c"
        source_text = windows_source.read_text(encoding="utf-8")
        if "SetFileAttributesW" in source_text:
            raise RuntimeError("Windows metadata restoration must apply attributes through the held staging handle")
        subprocess.run(
            [
                compiler(),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Werror",
                "-fshort-wchar",
                "-D_WIN32=1",
                f"-I{project / 'tests/runtime/windows_stubs'}",
                f"-I{project / 'src/runtime'}",
                "-fsyntax-only",
                str(windows_source),
            ],
            cwd=project,
            check=True,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
