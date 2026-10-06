#!/usr/bin/env python3
"""Native platform selection must fold before platform-specific code is inferred."""
from pathlib import Path
import subprocess
import sys
import tempfile


project = Path(sys.argv[1]).resolve()
compiler = Path(sys.argv[2]).resolve()
expected = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]
native_source = """import lib/platform.dl
if the build target is Windows:
    print "windows" as a line
if the build target is macOS:
    print "macos" as a line
if the build target is Linux:
    print "linux" as a line
"""
non_native_source = """import lib/platform.dl
if ((the build target is Windows) or (the build target is macOS)) or (the build target is Linux):
    @intrinsic("call pointer", 0 as a pointer, nothing)
"""

with tempfile.TemporaryDirectory(prefix="dynlex-platform-targets-") as directory:
    temporary = Path(directory)
    source = temporary / "native.dl"
    source.write_text(native_source)
    output = temporary / ("native.exe" if sys.platform == "win32" else "native.out")
    subprocess.run([str(compiler), str(source), "-o", str(output)], cwd=project, check=True)
    result = subprocess.run([str(output)], cwd=project, capture_output=True, text=True, check=True)
    if result.stdout != expected + "\n":
        raise RuntimeError(f"Expected only {expected!r}, received {result.stdout!r}")
    source.write_text(non_native_source)
    for mode, suffix in (("--emit-wasm", "wasm"), ("--emit-spirv", "spv")):
        subprocess.run(
            [str(compiler), str(source), mode, "-o", str(temporary / f"non-native.{suffix}")],
            cwd=project, check=True,
        )
