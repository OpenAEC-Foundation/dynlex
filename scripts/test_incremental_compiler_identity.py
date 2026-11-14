"""Executable padding must preserve build-ID identity and unchanged build reuse."""

import json
import os
import shutil
import subprocess


def check_compiler_identity(compiler, root):
    # ELF permits trailing padding. Signed executable formats cannot be modified
    # this way; their ordinary identity path is covered by every cache test.
    with compiler.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            return
    root.mkdir()
    main = root / "main.dl"
    main.write_text('@intrinsic("discard", 0)\n')
    output = root / "program"

    def invoke(executable):
        result = subprocess.run([str(executable), str(main), "-o", str(output),
                                 "--cache-dir", str(root / "cache"), "--incremental-stats"],
                                cwd=root, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return json.loads(next(line[13:] for line in result.stderr.splitlines()
                               if line.startswith("incremental: ")))

    original = invoke(compiler)
    output_time = output.stat().st_mtime_ns
    padded = root / "compiler"
    shutil.copyfile(compiler, padded)
    padded.chmod(compiler.stat().st_mode)
    page_size = os.sysconf("SC_PAGE_SIZE")
    with padded.open("ab") as stream:
        stream.write(bytes((-padded.stat().st_size) % page_size))
    for padding in (b"", b"\0"):
        with padded.open("ab") as stream:
            stream.write(padding)
        reused = invoke(padded)
        assert reused["cache_entry"] == original["cache_entry"], "padding changed a linker build ID"
        assert reused["frontend_reused"] and reused["objects_built"] == 0 and reused["link_ms"] == 0
        assert output.stat().st_mtime_ns == output_time
