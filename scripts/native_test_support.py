"""Select the native ABI toolchain used to build the compiler under test."""

from __future__ import annotations

import os
import re
from pathlib import Path


def resolve_c_compiler(compiler: Path) -> str:
    # Match the build's native ABI, including the Windows bootstrap toolchain.
    cache_path = compiler.parent / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"CMAKE_C_COMPILER:[^=]+=(.+)", line)
            if match and Path(match.group(1)).is_file():
                return match.group(1)
    return os.environ.get("CC", "cc")
