"""Native file builds use the system user cache without project configuration."""

import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid
from test_incremental_compiler_identity import check_compiler_identity


def windows_cache_directory():
    class Guid(ctypes.Structure):
        _fields_ = [("data1", ctypes.c_uint32), ("data2", ctypes.c_uint16),
                    ("data3", ctypes.c_uint16), ("data4", ctypes.c_ubyte * 8)]

    folder = Guid.from_buffer_copy(uuid.UUID("f1b32785-6fba-4fcf-9d55-7b8e7f157091").bytes_le)
    value = ctypes.c_wchar_p()
    result = ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(folder), 0, None, ctypes.byref(value))
    assert result == 0, "Windows could not determine LocalAppData"
    path = Path(value.value)
    ctypes.windll.ole32.CoTaskMemFree(ctypes.cast(value, ctypes.c_void_p))
    return path


def check_default_cache(compiler, root):
    root.mkdir()
    environment = os.environ.copy()
    if os.name == "nt":
        parent = windows_cache_directory()
    elif sys.platform == "darwin":
        parent = Path(subprocess.check_output(["getconf", "DARWIN_USER_CACHE_DIR"], text=True).strip())
    else:
        parent = root / "user-cache"
        environment["XDG_CACHE_HOME"] = str(parent)
    expected = parent / "dynlex" / "compiler" / "native-v1"
    source = ('integer means: @intrinsic("type", "int")\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n')
    main = root / "main.dl"
    main.write_text(source)
    output = "program.exe" if os.name == "nt" else "program"

    def invoke(*flags, cwd=root, success=True, env=environment):
        result = subprocess.run([str(compiler), "main.dl", "-o", output, *flags], cwd=cwd,
                                env=env, capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == success, result.stderr
        lines = [line[13:] for line in result.stderr.splitlines() if line.startswith("incremental: ")]
        return result, json.loads(lines[0]) if lines else None

    plain, stats = invoke()
    assert plain.stderr == "" and stats is None, "ordinary builds must keep cache statistics optional"
    _, warm = invoke("--incremental-stats")
    assert warm["frontend_reused"] and warm["objects_built"] == 0 and warm["link_ms"] == 0
    assert Path(warm["cache_directory"]).resolve() == expected.resolve()
    assert not (root / ".cache").exists(), "the default cache was created inside the project"

    manifest = Path(warm["cache_entry"])
    assert manifest.parent.resolve() == expected.resolve()
    original = manifest.read_bytes()
    try:
        manifest.write_bytes(b"corrupt")
        _, clean = invoke("--no-incremental")
        assert clean is None and manifest.read_bytes() == b"corrupt", "a clean build accessed the cache"
        failed, _ = invoke(success=False)
        assert "cache" in failed.stderr.lower(), "default cache corruption was hidden"
        _, uncached_output = invoke("--emit-llvm")
        assert uncached_output is None and manifest.read_bytes() == b"corrupt", "LLVM output accessed the native cache"
    finally:
        manifest.write_bytes(original)

    override = root / "chosen cache β"
    _, cold = invoke("--cache-dir", str(override), "--incremental-stats")
    assert not cold["frontend_reused"]
    _, reused = invoke("--cache-dir", str(override), "--incremental-stats")
    assert reused["frontend_reused"]
    assert Path(reused["cache_directory"]).resolve() == (override / "native-v1").resolve()
    invoke("--no-incremental", "--cache-dir", str(override), success=False)
    invoke("--no-incremental", "--incremental-stats", success=False)
    invoke("--emit-llvm", "--cache-dir", str(override), success=False)

    other = root / "other-project"
    other.mkdir()
    (other / "main.dl").write_text(source.replace('"first"', '"second"'))
    _, independent = invoke("--incremental-stats", cwd=other)
    assert not independent["frontend_reused"], "two projects shared a compilation result"
    _, reused = invoke("--incremental-stats", cwd=other)
    assert reused["frontend_reused"]
    runtime = subprocess.run([str(other / output)], capture_output=True, text=True, timeout=5)
    assert runtime.returncode == 0 and runtime.stdout == "second\n"

    if os.name != "nt" and sys.platform != "darwin":
        home = root / "home"
        home.mkdir()
        for value in ("", "relative-cache"):
            env = {**environment, "HOME": str(home), "XDG_CACHE_HOME": value}
            _, stats = invoke("--incremental-stats", env=env)
            assert Path(stats["cache_directory"]).resolve() == (home / ".cache/dynlex/compiler/native-v1").resolve()
    check_compiler_identity(compiler, root / "compiler-identity")
