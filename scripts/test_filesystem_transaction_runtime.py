#!/usr/bin/env python3

from __future__ import annotations

import os
import re
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
        cmake_text = (project / "CMakeLists.txt").read_text(encoding="utf-8")
        version_match = re.search(
            r"set\(DYNLEX_WINDOWS_TARGET_VERSION (0x[0-9A-F]+)\)",
            cmake_text,
        )
        if version_match is None or version_match.group(1) != "0x0A00":
            raise RuntimeError("Windows builds must consistently target Windows 10 or newer")
        windows_target_version = version_match.group(1)
        normal_target_definitions = re.search(
            r"if\(WIN32\).*?add_compile_definitions\((.*?)\).*?endif\(\)",
            cmake_text,
            re.DOTALL,
        )
        required_normal_definitions = {
            "NOMINMAX",
            "_WIN32_WINNT=${DYNLEX_WINDOWS_TARGET_VERSION}",
            "WINVER=${DYNLEX_WINDOWS_TARGET_VERSION}",
        }
        if normal_target_definitions is None or not required_normal_definitions.issubset(
            normal_target_definitions.group(1).split()
        ):
            raise RuntimeError("Normal Windows targets do not use the required platform definitions")
        for definition in (
            '"-D_WIN32_WINNT=${DYNLEX_WINDOWS_TARGET_VERSION}"',
            '"-DWINVER=${DYNLEX_WINDOWS_TARGET_VERSION}"',
        ):
            if definition not in cmake_text:
                raise RuntimeError(f"Windows runtime compilation omits {definition}")
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
                f"-D_WIN32_WINNT={windows_target_version}",
                f"-DWINVER={windows_target_version}",
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
