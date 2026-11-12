"""Build argument checks use inert tools and never configure or compile the repo."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

project = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="dynlex-build-arguments-") as directory:
    root = Path(directory)
    scripts = root / "scripts"
    scripts.mkdir()
    shutil.copy2(project / "scripts/build.sh", scripts / "build.sh")
    (scripts / "llvm_toolchain.sh").write_text(
        'dynlex_resolve_bootstrap_compilers() { DYNLEX_LLVM_BOOTSTRAP_CC=clang; '
        'DYNLEX_LLVM_BOOTSTRAP_CXX=clang++; }\n'
        'dynlex_ensure_llvm_toolchain() { DYNLEX_LLVM_NATIVE_INSTALL_DIR=unused; }\n'
    )
    tools = root / "tools"
    tools.mkdir()
    for name, command in (
        ("clang", ":"), ("clang++", ":"), ("cmake", ":"), ("git", ":"),
        ("nproc", "printf '9\\n'"),
        ("ninja", 'printf "%s\\n" "$@" > "$DYNLEX_BUILD_TEST_CAPTURE"'),
    ):
        tool = tools / name
        tool.write_text("#!/bin/sh\n" + command + "\n")
        tool.chmod(0o755)
    capture = root / "arguments"
    env = {**os.environ, "PATH": str(tools) + os.pathsep + os.environ["PATH"],
           "DYNLEX_BUILD_TEST_CAPTURE": capture.as_posix(), "CCACHE_DIR": (root / "ccache").as_posix()}

    def run(*arguments, success=True):
        capture.unlink(missing_ok=True)
        result = subprocess.run([os.environ.get("DYNLEX_TEST_BASH", "bash"), str(scripts / "build.sh"),
                                 "--lint=false", *arguments], cwd=root, env=env, capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        if success:
            return capture.read_text().splitlines()
        assert not capture.exists(), "an invalid argument started the build"

    assert run("-j1") == ["-j1"]
    assert run("-j", "2") == ["-j2"]
    assert run("--jobs=3") == ["-j3"]
    assert run("--jobs", "4") == ["-j4"]
    assert run() == ["-j9"]
    for arguments in (("-j",), ("--jobs",), ("--jobs=",), ("-j", ""), ("--jobs=0",), ("--jobs=oops",), ("--unknown",)):
        run(*arguments, success=False)
