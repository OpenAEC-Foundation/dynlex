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
integration_mode = sys.argv[2:] == ["--integration"]
compiler = project / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex")
source = '''integer means: @intrinsic("type", "int")
to alpha: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))
to beta: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))
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

    if integration_mode:
        for suite, fixture in (
            ("integration", "json_serialization"),
            ("integration", "string_lifecycle_counts"),
            ("integration", "managed_array_pointer_lifecycle"),
            ("required", "recursive_store_reinference"),
            ("required", "overloaded_type_constraint_inference"),
        ):
            entry = project / "tests" / suite / fixture / "main.dl"
            reference = build(False, entry=entry)
            generated = build(entry=entry)
            assert generated[1:] == reference[1:], f"partitioned code changed behavior: {fixture}"
            reused = build(entry=entry)
            assert reused[0]["frontend_reused"] and reused[0]["objects_built"] == 0
            assert reused[1:] == reference[1:], f"cached code changed behavior: {fixture}"
        print("Incremental native integration builds passed")
        raise SystemExit(0)

    cold = build()
    assert not cold[0]["frontend_reused"] and cold[0]["objects_built"] > 0
    clean = build(False)
    assert cold[1:] == clean[1:], "cold build differs from clean build"
    warm = build()
    assert warm[0]["frontend_reused"] and warm[0]["objects_built"] == 0
    assert warm[0]["objects_reused"] == cold[0]["objects_built"]
    assert warm[1:] == clean[1:], "warm build differs from clean build"
    main.write_text(source.replace("alpha\nbeta\n", "beta\nbeta\n"))
    same_file_edit = build()
    assert same_file_edit[0]["matcher_cache_hits"] > 0, "code edit discarded unchanged same-file matches"
    assert same_file_edit[1:] == build(False)[1:], "same-file match reuse differs from clean build"
    repeated_calls = "alpha\nbeta\n" * 20
    main.write_text(source.replace("alpha\nbeta\n", repeated_calls))
    build()
    unrelated_definition = 'to gamma: @intrinsic("discard", 1)\n'
    main.write_text(source.replace("alpha\nbeta\n", unrelated_definition + repeated_calls))
    unrelated = build()
    assert unrelated[0]["matcher_cache_hits"] >= 20, "unrelated new pattern invalidated unchanged calls"
    assert unrelated[1:] == build(False)[1:], "unrelated new pattern changed cached matches"
    main.write_text(source.replace("alpha\nbeta\n", repeated_calls))
    build()
    main.write_text('@intrinsic("discard", 1)\n' + source.replace("alpha\nbeta\n", repeated_calls))
    shifted = build()
    assert shifted[0]["matcher_cache_hits"] >= 20, "line insertion invalidated unchanged definitions and calls"
    assert shifted[1:] == build(False)[1:], "line insertion changed cached matches"
    main.write_text(source)
    build()
    main.write_text("# A new source comment must not invalidate executable code.\n" + source)
    commented = build()
    assert commented[0]["frontend_reused"], "comment-only edit rebuilt the frontend"
    assert commented[1:] == build(False)[1:], "comment-only reuse differs from clean build"
    main.write_text(source)
    assert build()[0]["frontend_reused"], "removing a source comment rebuilt the frontend"
    matcher_entries = {entry: entry.read_bytes() for entry in (cache / "native-v1").glob("matcher-*.entry")}
    assert matcher_entries, "incremental build did not persist its matcher cache separately"
    for entry, data in matcher_entries.items():
        entry.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
    main.write_text("# Unchanged executable code should not need matcher data.\n" + source)
    assert build()[0]["frontend_reused"], "comment-only build read the matcher cache"
    main.write_text(source.replace("alpha\nbeta\n", "beta\nalpha\n"))
    assert "corrupt cache entry" in build(success=False)[1], "edited build ignored matcher cache corruption"
    for entry, data in matcher_entries.items():
        entry.write_bytes(data)
    main.write_text(source)
    assert build()[0]["frontend_reused"]
    for entry in matcher_entries:
        entry.unlink()
    main.write_text(source.replace("alpha\nbeta\n", "beta\nalpha\n"))
    evicted = build()
    assert evicted[0]["matcher_cache_hits"] == 0, "evicted matcher results were reused"
    assert evicted[1:] == build(False)[1:], "evicted matcher cache changed compilation behavior"
    main.write_text(source)
    build()
    critical = root / "critical.dl"
    critical.write_text('integer means: @intrinsic("type", "int")\n'
                        'to alpha: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n')
    main.write_text("import critical.dl\nalpha\n")
    build()
    critical.write_text("# Comment in a used imported file.\n" + critical.read_text())
    critical_comment = build()
    assert critical_comment[0]["frontend_reused"], "comment in a used import rebuilt the frontend"
    assert critical_comment[1:] == build(False)[1:], "import comment reuse differs from clean build"
    critical.write_text(critical.read_text() + "alpha\n")
    main.write_text("import critical.dl\nalpha\n")
    build()
    main.write_text("import critical.dl\nalpha\nalpha\n")
    body_edit = build()
    assert body_edit[0]["matcher_cache_hits"] > 0, "code edit did not reuse matches in an unchanged file"
    assert body_edit[1:] == build(False)[1:], "cached matches changed code edit behavior"
    main.write_text("import critical.dl\nalpha\n")
    removed_call = build()
    assert removed_call[0]["matcher_cache_hits"] > 0, "removed call discarded unrelated cached matches"
    assert removed_call[1:] == build(False)[1:], "removed call differs from clean build"
    main.write_text('import critical.dl\n'
                    'to alpha: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "other"))\n'
                    'alpha\n')
    added_overload = build(success=False)
    assert added_overload[1] == build(False, success=False)[1], "new overload reused a stale match"
    data_helper = root / "data_helper.dl"
    data_helper.write_text((project / "tests/required/deferred_data_sections_nested/main.dl").read_text())
    main.write_text("import data_helper.dl\n")
    build()
    main.write_text('import data_helper.dl\n@intrinsic("discard", 1)\n')
    data_edit = build()
    assert data_edit[0]["matcher_cache_hits"] > 0, "data sections disabled all matcher reuse"
    assert data_edit[1:] == build(False)[1:], "cached data section match differs from clean build"
    main.write_text(source)
    build()
    main.write_text(source.replace('"alpha"))', '"alpha # inside a string"))'))
    changed_string = build()
    assert not changed_string[0]["frontend_reused"], "comment marker inside a string was ignored"
    assert changed_string[0]["matcher_cache_hits"] > 0, "one-line body edit invalidated unrelated matches"
    assert changed_string[1:] == build(False)[1:], "string edit differs from clean build"
    pack_source = (project / "tests/required/argument_pack_capture/main.dl").read_text()
    pack_main = root / "pack.dl"
    pack_main.write_text(pack_source)
    build(entry=pack_main)
    pack_main.write_text(pack_source.replace("count 1, 2 and 3", "count 4, 5 and 6"))
    edited_pack = build(entry=pack_main)
    assert edited_pack[0]["matcher_cache_hits"] > 0, "argument pack edit discarded unrelated matches"
    assert edited_pack[1:] == build(False, entry=pack_main)[1:], "cached argument pack match differs from clean build"
    main.write_text(source)
    build()
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
    configured_source = main.read_text()
    main.write_text("// A comment with the configured prefix.\n" + configured_source)
    assert build()[0]["frontend_reused"], "configured comment syntax rebuilt the frontend"
    main.write_text(configured_source)
    assert build()[0]["frontend_reused"], "removing a configured comment rebuilt the frontend"
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
