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
    raise RuntimeError("A Clang C compiler is required for path/host runtime tests")


def compile_and_run(project: Path, sources: list[Path], include_runtime: bool = True) -> None:
    with tempfile.TemporaryDirectory(prefix="dynlex-path-host-test-") as temporary:
        executable = Path(temporary) / "test"
        command = [
            compiler(),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-I{project / 'src/runtime'}",
            *(str(source) for source in sources),
        ]
        if include_runtime:
            command.extend(
                str(project / relative)
                for relative in (
                    "src/runtime/pathRuntime.c",
                    "src/runtime/pathUriRuntime.c",
                    "src/runtime/runtimeError.c",
                    "src/runtime/runtimeText.c",
                )
            )
        command.extend(("-o", str(executable)))
        subprocess.run(command, cwd=project, check=True)
        subprocess.run([executable], cwd=project, check=True)


def verify_windows_binary_stdin(project: Path) -> None:
    source = (project / "src/runtime/hostRuntimeWindows.c").read_text(encoding="utf-8")
    required = (
        "#include <fcntl.h>",
        "#include <io.h>",
        "_setmode(_fileno(stdin), _O_BINARY)",
    )
    missing = [text for text in required if text not in source]
    if missing:
        raise RuntimeError(f"Windows standard input is not forced to binary mode: {missing}")


def main() -> int:
    project = Path(__file__).resolve().parent.parent
    compile_and_run(
        project,
        [
            project / "tests/runtime/host_runtime_windows_path.c",
            project / "src/runtime/hostRuntimeWindowsPath.c",
        ],
        include_runtime=False,
    )
    verify_windows_binary_stdin(project)
    if os.name != "nt":
        compile_and_run(project, [project / "tests/runtime/path_uri_symlink_identity.c"])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
