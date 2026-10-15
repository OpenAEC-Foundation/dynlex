#!/usr/bin/env python3
"""Compare cold, reused, edited and invalidated native builds to clean builds."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

project = Path(sys.argv[1]).resolve()
compiler = project / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex")
source = '''integer means: @intrinsic("type", "int")
function alpha:
    execute:
        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))
function beta:
    execute:
        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))
alpha
beta
'''

with tempfile.TemporaryDirectory(prefix="dynlex-incremental-test-") as temporary:
    root = Path(temporary)
    cache = root / "cache"
    main = root / "main.dl"
    output = root / ("program.exe" if os.name == "nt" else "program")
    main.write_text(source)

    def build(incremental=True, flags=(), success=True, entry=main):
        arguments = [str(compiler), str(entry), *flags, "-o", str(output)]
        if incremental:
            arguments += ["--incremental", str(cache), "--incremental-stats"]
        result = subprocess.run(arguments, cwd=root, capture_output=True, text=True, timeout=120)
        if (result.returncode == 0) != success:
            raise AssertionError(f"wrong build outcome: {arguments}\n{result.stdout}\n{result.stderr}")
        lines = result.stderr.splitlines(keepends=True)
        statistics = [line for line in lines if line.startswith("incremental: ")]
        stats = json.loads(statistics[0].removeprefix("incremental: ")) if statistics else None
        diagnostics = "".join(line for line in lines if not line.startswith("incremental: "))
        runtime = None
        if success:
            runtime = subprocess.run([str(output)], cwd=root, capture_output=True, timeout=10)
            if runtime.returncode:
                raise AssertionError(f"execution failed: {runtime.stderr!r}")
        return stats, diagnostics, runtime.stdout if runtime else None

    cold = build()
    assert not cold[0]["frontend_reused"] and cold[0]["objects_built"] > 0
    clean = build(False)
    assert cold[1:] == clean[1:], "cold build differs from clean build"
    warm = build()
    assert warm[0]["frontend_reused"] and warm[0]["objects_built"] == 0
    assert warm[0]["objects_reused"] == cold[0]["objects_built"]
    assert warm[1:] == clean[1:], "warm build differs from clean build"
    output.unlink()
    assert build()[2] == clean[2], "cache failed to recreate a deleted executable"

    main.write_text(source.replace('"alpha"))', '"gamma"))'))
    edited = build()
    assert not edited[0]["frontend_reused"]
    assert edited[0]["objects_reused"] > 0 and edited[0]["objects_built"] > 0, "edit did not selectively rebuild objects"
    assert edited[1:] == build(False)[1:], "edited incremental build differs from clean build"

    for flags in [("-O2",), ("-g",), ("-ffast-math",), ("--trace-execution",)]:
        changed = build(flags=flags)
        assert not changed[0]["frontend_reused"], "option change retained frontend result"
        assert changed[1:] == build(False, flags)[1:], f"options changed semantics: {flags}"
        reused = build(flags=flags)
        assert reused[0]["frontend_reused"] and reused[0]["objects_built"] == 0
        assert reused[1:] == changed[1:]
        if flags == ("-g",) and sys.platform.startswith("linux"):
            # Resolve the compiler's pinned verifier through its existing helper.
            from test_debug_info import resolve_native_llvm_install_directory
            install = resolve_native_llvm_install_directory()
            assert install is not None
            verified = subprocess.run([str(install / "bin" / "llvm-dwarfdump"), "--verify", str(output)],
                                      capture_output=True, text=True, timeout=30)
            assert verified.returncode == 0, verified.stdout + verified.stderr

    config = root / "config.dl"
    config.write_text('dynlex options:\n    comment: "//"\n')
    configured = build()
    assert not configured[0]["frontend_reused"], "new syntax config was ignored"
    assert configured[1:] == build(False)[1:]
    config.unlink()
    assert not build()[0]["frontend_reused"], "deleted syntax config was ignored"

    original_time = main.stat().st_mtime_ns
    main.write_text("this is an undefined expression\n")
    os.utime(main, ns=(original_time, original_time))
    broken = build(success=False)
    reference = build(False, success=False)
    assert broken[1] == reference[1], "invalid source reused old diagnostics or code"
    main.write_text(source)
    assert build()[1:] == build(False)[1:]

    nested = root / "nested"
    nested.mkdir()
    imported_main = nested / "main.dl"
    imported_main.write_text("import helper.dl\n")
    helper = root / "helper.dl"
    helper.write_text(source)
    first_import = build(entry=imported_main)
    assert first_import[1:] == build(False, entry=imported_main)[1:]
    assert build(entry=imported_main)[0]["frontend_reused"]
    (nested / "helper.dl").write_text(source.replace('"alpha"))', '"delta"))'))
    shadow = build(entry=imported_main)
    assert not shadow[0]["frontend_reused"], "new higher-priority import was ignored"
    assert shadow[1:] == build(False, entry=imported_main)[1:]
    (nested / "helper.dl").unlink()
    helper.write_text(source.replace('"alpha"))', '"omega"))'))
    assert build(entry=imported_main)[1:] == build(False, entry=imported_main)[1:]
    helper.unlink()
    assert build(success=False, entry=imported_main)[1] == build(False, success=False, entry=imported_main)[1]

    for fixture in ("json_serialization", "string_lifecycle_counts", "managed_array_pointer_lifecycle",
                    "recursive_store_reinference", "overloaded_type_constraint_inference"):
        entry = project / "tests" / "required" / fixture / "main.dl"
        reference = build(False, entry=entry)
        generated = build(entry=entry)
        assert generated[1:] == reference[1:], f"partitioned code changed behavior: {fixture}"
        reused = build(entry=entry)
        assert reused[0]["frontend_reused"] and reused[0]["objects_built"] == 0
        assert reused[1:] == reference[1:], f"cached code changed behavior: {fixture}"

    main.write_text(source)
    build()
    # Exercise bounded eviction with a sparse old entry; unrelated files stay intact.
    namespace = cache / "native-v1"
    unrelated = namespace / "notes.entry"
    unrelated.write_text("user file")
    old = namespace / ("object-" + "f" * 64 + ".entry")
    with old.open("wb") as sparse:
        sparse.truncate(512 * 1024 * 1024 + 1)
    os.utime(old, (1, 1))
    assert build()[0]["frontend_reused"]
    assert not old.exists() and unrelated.read_text() == "user file", "eviction deleted the wrong entry"

    # Damage all option variants so the current program certainly encounters one.
    entries = list((cache / "native-v1").glob("object-*.entry"))
    for entry in entries:
        data = entry.read_bytes()
        entry.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    corrupted = build(success=False)
    assert "corrupt cache entry" in corrupted[1], "cache corruption was hidden"
    assert corrupted[0] is None or corrupted[0]["objects_built"] == 0

print("Incremental native builds, invalidation, partial reuse, diagnostics and corruption checks passed")
