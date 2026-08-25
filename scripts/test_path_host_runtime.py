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


def verify_windows_cache_directory(project: Path) -> None:
    source = (project / "src/runtime/hostRuntimeWindows.c").read_text(encoding="utf-8")
    cmake = (project / "CMakeLists.txt").read_text(encoding="utf-8")
    required_source = (
        "#include <shlobj.h>",
        "SHGetKnownFolderPath",
        "FOLDERID_LocalAppData",
        "CoTaskMemFree",
    )
    missing_source = [text for text in required_source if text not in source]
    if missing_source:
        raise RuntimeError(f"Windows user cache directory API is incomplete: {missing_source}")
    required_libraries = ("shell32", "ole32", "uuid")
    missing_libraries = [library for library in required_libraries if library not in cmake]
    if missing_libraries:
        raise RuntimeError(f"Windows user cache directory libraries are not linked: {missing_libraries}")


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
    verify_windows_cache_directory(project)
    if os.name != "nt":
        compile_and_run(
            project,
            [
                project / "tests/runtime/runtime_error.c",
                project / "src/runtime/runtimeError.c",
            ],
            include_runtime=False,
        )
        compile_and_run(project, [project / "tests/runtime/path_uri_symlink_identity.c"])
        compile_and_run(
            project,
            [
                project / "tests/runtime/host_runtime_posix_cache.c",
                project / "src/runtime/hostRuntimePosix.c",
                project / "src/runtime/runtimeError.c",
                project / "src/runtime/runtimeText.c",
            ],
            include_runtime=False,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
