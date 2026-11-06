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
            assert reused[0]["frontend_reused"] and reused[0]["linked_cached_objects"]
            assert reused[0]["objects_built"] == 0
            assert reused[1:] == reference[1:], f"cached code changed behavior: {fixture}"
        print("Incremental native integration builds passed")
        raise SystemExit(0)

    cold = build()
    assert not cold[0]["frontend_reused"] and not cold[0]["linked_cached_objects"]
    assert cold[0]["objects_built"] > 0
    clean = build(False)
    assert cold[1:] == clean[1:], "cold build differs from clean build"
    warm = build()
    assert warm[0]["frontend_reused"] and warm[0]["linked_cached_objects"]
    assert warm[0]["objects_built"] == 0
    assert warm[0]["objects_reused"] == cold[0]["objects_built"]
    assert warm[1:] == clean[1:], "warm build differs from clean build"
    next((cache / "native-v1").glob("object-*.entry")).unlink()
    recovered = build()
    assert recovered[0]["frontend_reused"] and not recovered[0]["linked_cached_objects"]
    assert recovered[0]["objects_built"] == 1, "evicted object was not rebuilt from cached IR"
    assert recovered[1:] == clean[1:], "evicted object changed compilation behavior"
    main.write_text(source.replace("alpha\nbeta\n", "beta\nbeta\n"))
    same_file_edit = build()
    assert same_file_edit[0]["matcher_cache_hits"] > 0, "code edit discarded unchanged same-file matches"
    assert same_file_edit[0]["precedence_matcher_cache_hits"] > 0, "code edit reran unchanged precedence matches"
    assert same_file_edit[1:] == build(False)[1:], "same-file match reuse differs from clean build"
    repeated_calls = "alpha\nbeta\n" * 20
    main.write_text(source.replace("alpha\nbeta\n", repeated_calls))
    build()
    unrelated_definition = 'to gamma: @intrinsic("discard", 1)\n'
    main.write_text(source.replace("alpha\nbeta\n", unrelated_definition + repeated_calls))
    unrelated = build()
    assert unrelated[0]["matcher_cache_hits"] >= 20, "unrelated new pattern invalidated unchanged calls"
    assert unrelated[0]["precedence_matcher_cache_hits"] >= 20, "unrelated new pattern invalidated precedence calls"
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
    assert commented[0]["linked_cached_objects"], "comment-only edit reparsed LLVM IR"
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
    warning_helper = root / "warning_helper.dl"
    warning_main = root / "warning_main.dl"
    helper_source = 'to greet: @intrinsic("discard", 1)\n'
    warning_source = ('import warning_helper.dl\n'
                      'integer means: @intrinsic("type", "int")\n'
                      'to show value as the value: @intrinsic("discard", value)\n'
                      'show 1 as the value\n'
                      'greet\n')
    warning_helper.write_text(helper_source)
    warning_main.write_text(warning_source)
    warning_cold = build(entry=warning_main)
    assert "Warning:" in warning_cold[1], "diagnostic dependency test produced no warning"
    warning_helper.write_text("# Comment in a used file with no diagnostic ranges.\n" + helper_source)
    warning_comment = build(entry=warning_main)
    assert warning_comment[0]["frontend_reused"] and warning_comment[0]["linked_cached_objects"]
    assert warning_comment[1:] == warning_cold[1:], "unrelated comment changed cached diagnostics"
    warning_main.write_text("# Comment before the warning.\n" + warning_source)
    moved_warning = build(entry=warning_main)
    assert not moved_warning[0]["frontend_reused"], "shifted warning kept its old source location"
    assert moved_warning[1:] == build(False, entry=warning_main)[1:]
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
    assert changed_string[0]["precedence_matcher_cache_hits"] > 0, "one-line body edit invalidated precedence matches"
    assert changed_string[1:] == build(False)[1:], "string edit differs from clean build"
    unused_source = source + 'to unused:\n    @intrinsic("discard", 1)\n'
    main.write_text(unused_source)
    build()
    main.write_text(unused_source.replace('"discard", 1', '"discard", 2'))
    unused_edit = build()
    assert not unused_edit[0]["frontend_reused"], "unused body edit skipped source validation"
    assert unused_edit[0]["type_inference_reused"], "unused body edit repeated whole-program type inference"
    assert unused_edit[0]["linked_cached_objects"], "identical generated code did not reuse cached objects directly"
    assert unused_edit[0]["objects_built"] == 0
    assert unused_edit[1:] == build(False)[1:], "unused body edit differs from clean build"
    main.write_text(unused_source.replace('"discard", 1)', '"discard", 2)\n    @intrinsic("discard", 3)'))
    inserted_unused = build()
    assert inserted_unused[0]["type_inference_reused"], "unused body insertion repeated type inference"
    assert inserted_unused[1:] == build(False)[1:], "unused body insertion differs from clean build"
    main.write_text(unused_source)
    removed_unused = build()
    assert removed_unused[0]["type_inference_reused"], "unused body deletion repeated type inference"
    assert removed_unused[1:] == build(False)[1:], "unused body deletion differs from clean build"
    main.write_text(unused_source.replace('@intrinsic("discard", 1)', 'unresolved function call'))
    invalid_unused = build(success=False)
    assert invalid_unused[1] == build(False, success=False)[1], "unused body edit hid a resolution error"
    precedence_source = '''function left plus right:
    RULE: $ times $
    replacement:
        @intrinsic("add", left, right)
function left times right:
    replacement:
        @intrinsic("multiply", left, right)
@intrinsic("return", @intrinsic("subtract", 1 plus 2 times 3, EXPECTED))
'''
    main.write_text(precedence_source.replace("RULE", "before").replace("EXPECTED", "9"))
    build()
    main.write_text(precedence_source.replace("RULE", "after").replace("EXPECTED", "7"))
    precedence_edit = build()
    assert precedence_edit[0]["precedence_matcher_cache_hits"] == 0, "changed precedence reused stale matches"
    assert precedence_edit[1:] == build(False)[1:], "precedence edit differs from clean build"
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
    assert not edited[0]["type_inference_reused"], "called function edit kept stale inference"
    assert edited[0]["objects_built"] > 0, f"changed function did not rebuild its object: {edited[0]}"
    assert edited[1:] == build(False)[1:], "edited incremental build differs from clean build"

    leaf_a = root / "leaf_a.dl"
    leaf_b = root / "leaf_b.dl"
    leaf_helper = root / "leaf_helper.dl"
    leaf_types = root / "leaf_types.dl"
    leaf_main = root / "leaf_main.dl"
    leaf_types.write_text('integer means: @intrinsic("type", "int")\n')
    leaf_helper.write_text('to helper:\n    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "helper"))\n')
    leaf_a.write_text('to alpha:\n    helper\n    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\nto sibling:\n    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "sibling"))\n')
    leaf_b.write_text('to beta:\n    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "second"))\n')
    leaf_main.write_text('import leaf_types.dl\nimport leaf_helper.dl\nimport leaf_a.dl\nimport leaf_b.dl\nalpha\nsibling\nbeta\n')
    build(entry=leaf_main)
    leaf_a.write_text(leaf_a.read_text().replace('"first"', '"changed"'))
    leaf_edit = build(entry=leaf_main)
    assert leaf_edit[0]["functions_ir_reused"] >= 3, "active edit regenerated independent or same-file function IR"
    assert leaf_edit[1:] == build(False, entry=leaf_main)[1:], "reused function IR changed active edit behavior"
    leaf_b.write_text(leaf_b.read_text().replace('"second"', '"updated"'))
    caller_edit = build(entry=leaf_main)
    assert caller_edit[0]["functions_ir_reused"] >= 2, "unchanged caller and callee were regenerated"
    assert caller_edit[1:] == build(False, entry=leaf_main)[1:], "reused call chain changed active edit behavior"
    leaf_helper.write_text(leaf_helper.read_text().replace('"helper"', '"replacement"'))
    dependency_edit = build(entry=leaf_main)
    assert dependency_edit[0]["inference_instantiations_reused"] >= 3, f"unchanged caller was re-inferred despite equivalent callee interface: {dependency_edit[0]}"
    assert dependency_edit[0]["functions_ir_reused"] >= 3, f"unchanged caller IR was regenerated despite equivalent callee interface: {dependency_edit[0]}"
    assert dependency_edit[1:] == build(False, entry=leaf_main)[1:], "changed callee retained stale caller IR"
    leaf_helper.write_text('to helper:\n    @intrinsic("discard", 1)\n')
    effect_edit = build(entry=leaf_main)
    assert effect_edit[0]["inference_instantiations_reused"] == 2, "changed callee effect reused caller inference"
    assert effect_edit[1:] == build(False, entry=leaf_main)[1:], "changed callee effect kept stale caller code"

    typed_dependency = root / "typed_dependency.dl"
    typed_dependency_source = ('integer means: @intrinsic("type", "int")\n'
                               'to helper {integer:value}:\n'
                               '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
                               'to caller:\n'
                               '    helper 1\n'
                               'caller\n')
    typed_dependency.write_text(typed_dependency_source)
    build(entry=typed_dependency)
    typed_dependency.write_text(typed_dependency_source.replace('"first"', '"updated"'))
    typed_dependency_edit = build(entry=typed_dependency)
    assert typed_dependency_edit[0]["inference_instantiations_reused"] >= 1, "typed callee edit re-inferred caller"
    assert typed_dependency_edit[0]["functions_ir_reused"] >= 1, "typed callee edit regenerated caller IR"
    assert typed_dependency_edit[1:] == build(False, entry=typed_dependency)[1:], "typed replacement differs from clean build"

    multi_dependency = root / "multi_dependency.dl"
    multi_dependency_source = ('integer means: @intrinsic("type", "int")\n'
                               'to helper {value}:\n'
                               '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
                               'to caller:\n'
                               '    helper 1\n'
                               '    helper 1.0\n'
                               'caller\n')
    multi_dependency.write_text(multi_dependency_source)
    build(entry=multi_dependency)
    multi_dependency.write_text(multi_dependency_source.replace('"before"', '"after"'))
    multi_dependency_edit = build(entry=multi_dependency)
    assert multi_dependency_edit[0]["inference_instantiations_reused"] >= 1, "multi-instantiation callee edit re-inferred caller"
    assert multi_dependency_edit[0]["functions_ir_reused"] >= 1, "multi-instantiation callee edit regenerated caller IR"
    assert multi_dependency_edit[1:] == build(False, entry=multi_dependency)[1:], "multi-instantiation replacement differs from clean build"

    invalid_dependency = root / "invalid_dependency.dl"
    invalid_dependency_source = ('integer means: @intrinsic("type", "int")\n'
                                 'to helper:\n'
                                 '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
                                 'to caller: helper\n'
                                 'caller\n')
    invalid_dependency.write_text(invalid_dependency_source)
    build(entry=invalid_dependency)
    invalid_dependency.write_text(invalid_dependency_source.replace('to caller:', '    @intrinsic("add", 1, "x")\nto caller:'))
    assert build(success=False, entry=invalid_dependency)[1] == build(False, success=False, entry=invalid_dependency)[1], "failed early inference duplicated or changed the diagnostic"

    library_main = root / "library_main.dl"
    library_source = ('real means: @intrinsic("type", "float", 64)\n'
                      'to trig:\n'
                      '    @intrinsic("discard", @intrinsic("call", "m", "sin", real, 1.0))\n'
                      'to other:\n'
                      '    @intrinsic("discard", 1)\n'
                      'trig\nother\n')
    library_main.write_text(library_source)
    build(entry=library_main)
    library_main.write_text(library_source.replace('"discard", 1)', '"discard", 2)'))
    library_edit = build(entry=library_main)
    assert library_edit[0]["functions_ir_reused"] > 0, "external-call function IR was regenerated"
    assert library_edit[1:] == build(False, entry=library_main)[1:], "reused function lost its link library"

    partition_main = root / "partition_main.dl"
    partition_main.write_text('integer means: @intrinsic("type", "int")\n' +
                              ''.join(f'import part{i:02d}.dl\n' for i in range(20)) +
                              ''.join(f'part{i:02d}\n' for i in range(20)))
    for index in range(20):
        (root / f"part{index:02d}.dl").write_text(
            f"to part{index:02d}:\n" +
            ''.join('    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, '
                    f'"part{index:02d}-{line:02d}"))\n' for line in range(5)))
    build(entry=partition_main)
    edited_partition = root / "part07.dl"
    edited_partition.write_text(edited_partition.read_text().replace('"part07-00"', '"changed"'))
    partition_edit = build(entry=partition_main)
    assert partition_edit[0]["inference_instantiations_reused"] >= 19, f"unchanged function bodies were re-inferred: {partition_edit[0]}"
    assert partition_edit[0]["functions_ir_reused"] == 19
    assert partition_edit[0]["objects_built"] <= 3, f"one function edit rebuilt unrelated object partitions: {partition_edit[0]}"
    assert partition_edit[1:] == build(False, entry=partition_main)[1:]
    for index in (8, 9):
        path = root / f"part{index:02d}.dl"
        path.write_text(path.read_text().replace(f'"part{index:02d}-00"', f'"updated-{index}"'))
    multi_file_edit = build(entry=partition_main)
    assert multi_file_edit[0]["inference_instantiations_reused"] == 18
    assert multi_file_edit[0]["functions_ir_reused"] == 18
    assert multi_file_edit[1:] == build(False, entry=partition_main)[1:]

    class_main = root / "class_main.dl"
    class_source = ('integer means: @intrinsic("type", "int")\n'
                    'class:\n'
                    '    patterns:\n'
                    '        a record\n'
                    '    members:\n'
                    '        value\n'
                    'to alpha:\n'
                    '    @intrinsic("discard", @intrinsic("construct", a record, 1))\n'
                    '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                    'to beta:\n'
                    '    @intrinsic("discard", @intrinsic("construct", a record, 2))\n'
                    '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                    'alpha\nbeta\n')
    class_main.write_text(class_source)
    build(entry=class_main)
    class_main.write_text(class_source.replace('"alpha"))', '"changed"))'))
    class_edit = build(entry=class_main)
    assert class_edit[0]["inference_instantiations_reused"] >= 1, "class program re-inferred unchanged function"
    assert class_edit[0]["functions_ir_reused"] >= 1, "class program regenerated unchanged function IR"
    assert class_edit[1:] == build(False, entry=class_main)[1:], "class edit differs from clean build"

    argument_main = root / "argument_main.dl"
    argument_source = ('integer means: @intrinsic("type", "int")\n'
                       'to alpha {integer:value}:\n'
                       '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                       'to beta {integer:value}:\n'
                       '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                       'alpha 1\nbeta 2\n')
    argument_main.write_text(argument_source)
    build(entry=argument_main)
    argument_main.write_text(argument_source.replace('"alpha"))', '"changed"))'))
    argument_edit = build(entry=argument_main)
    assert argument_edit[0]["inference_instantiations_reused"] >= 1, "typed argument function was re-inferred"
    assert argument_edit[0]["functions_ir_reused"] >= 1, "typed argument function IR was regenerated"
    assert argument_edit[1:] == build(False, entry=argument_main)[1:], "typed argument edit differs from clean build"

    result_main = root / "result_main.dl"
    result_source = ('integer means: @intrinsic("type", "int")\n'
                     'function alpha:\n'
                     '    execute:\n'
                     '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                     '        @intrinsic("return", 41)\n'
                     'function beta:\n'
                     '    execute:\n'
                     '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                     '        @intrinsic("return", 42)\n'
                     '@intrinsic("discard", alpha)\n@intrinsic("discard", beta)\n')
    result_main.write_text(result_source)
    build(entry=result_main)
    result_main.write_text(result_source.replace('"alpha"))', '"changed"))'))
    result_edit = build(entry=result_main)
    assert result_edit[0]["inference_instantiations_reused"] >= 1, "runtime result function was re-inferred"
    assert result_edit[0]["functions_ir_reused"] >= 1, "runtime result function IR was regenerated"
    assert result_edit[1:] == build(False, entry=result_main)[1:], "runtime result edit differs from clean build"

    warning_code_main = root / "warning_code_main.dl"
    warning_code_source = ('integer means: @intrinsic("type", "int")\n'
                           'to show value as the value: @intrinsic("discard", value)\n'
                           'to alpha:\n'
                           '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                           'to beta:\n'
                           '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                           'alpha\nbeta\n')
    warning_code_main.write_text(warning_code_source)
    warning_cold = build(entry=warning_code_main)
    assert "Warning:" in warning_cold[1]
    warning_code_main.write_text(warning_code_source.replace('"alpha"))', '"changed"))'))
    warning_edit = build(entry=warning_code_main)
    assert warning_edit[0]["functions_ir_reused"] >= 1, "warning disabled safe function IR reuse"
    assert warning_edit[1:] == build(False, entry=warning_code_main)[1:], "warning changed after code edit"

    global_main = root / "global_main.dl"
    global_source = ('integer means: @intrinsic("type", "int")\n'
                     '@intrinsic("store", counter, 1)\n'
                     'function read state:\n'
                     '    globals:\n'
                     '        counter\n'
                     '    execute:\n'
                     '        @intrinsic("return", counter)\n'
                     'to other:\n'
                     '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "other"))\n'
                     'other\n@intrinsic("return", @intrinsic("subtract", read state, 1))\n')
    global_main.write_text(global_source)
    build(entry=global_main)
    global_main.write_text(global_source.replace('"other"))', '"changed"))'))
    global_edit = build(entry=global_main)
    assert global_edit[0]["functions_ir_reused"] >= 1, "unchanged global reader IR was regenerated"
    assert global_edit[1:] == build(False, entry=global_main)[1:], "global reader edit differs from clean build"
    global_main.write_text(global_main.read_text().replace('counter, 1)', 'counter, 2)').replace('read state, 1)', 'read state, 2)'))
    global_initializer_edit = build(entry=global_main)
    assert global_initializer_edit[0]["functions_ir_reused"] == 0, "changed global initializer reused old function IR"
    assert global_initializer_edit[1:] == build(False, entry=global_main)[1:]

    indirect_global_main = root / "indirect_global_main.dl"
    indirect_global_source = ('integer means: @intrinsic("type", "int")\n'
                              '@intrinsic("store", counter, 1)\n'
                              'function read state:\n'
                              '    globals:\n'
                              '        counter\n'
                              '    execute:\n'
                              '        @intrinsic("return", counter)\n'
                              'to helper:\n'
                              '    @intrinsic("discard", read state)\n'
                              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "old"))\n'
                              'to caller: helper\n'
                              'caller\n')
    indirect_global_main.write_text(indirect_global_source)
    build(entry=indirect_global_main)
    indirect_global_main.write_text(indirect_global_source.replace('"old"', '"new"'))
    assert build(entry=indirect_global_main)[1:] == build(False, entry=indirect_global_main)[1:], "indirect global reader was pre-inferred before global initialization"

    writer_main = root / "writer_main.dl"
    writer_source = ('integer means: @intrinsic("type", "int")\n'
                     '@intrinsic("store", counter, 0)\n'
                     'function alpha:\n'
                     '    globals:\n'
                     '        counter\n'
                     '    execute:\n'
                     '        @intrinsic("store", counter, 1)\n'
                     '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                     'function beta:\n'
                     '    globals:\n'
                     '        counter\n'
                     '    execute:\n'
                     '        @intrinsic("store", counter, 2)\n'
                     '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                     'alpha\nbeta\n'
                     '@intrinsic("return", @intrinsic("subtract", counter, 2))\n')
    writer_main.write_text(writer_source)
    build(entry=writer_main)
    writer_main.write_text(writer_source.replace('"alpha"))', '"changed"))'))
    writer_edit = build(entry=writer_main)
    assert writer_edit[0]["inference_instantiations_reused"] >= 1, "global writer was re-inferred"
    assert writer_edit[0]["functions_ir_reused"] >= 1, "global writer IR was regenerated"
    assert writer_edit[1:] == build(False, entry=writer_main)[1:], "global writer edit differs from clean build"

    class_writer_main = root / "class_writer_main.dl"
    class_writer_source = ('integer means: @intrinsic("type", "int")\n'
                           'class:\n'
                           '    patterns:\n'
                           '        a record\n'
                           '    members:\n'
                           '        value\n'
                           '@intrinsic("store", state, @intrinsic("construct", a record, 0))\n'
                           'function alpha:\n'
                           '    globals:\n'
                           '        state\n'
                           '    execute:\n'
                           '        @intrinsic("store", state, @intrinsic("construct", a record, 1))\n'
                           '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                           'function beta:\n'
                           '    globals:\n'
                           '        state\n'
                           '    execute:\n'
                           '        @intrinsic("store", state, @intrinsic("construct", a record, 2))\n'
                           '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                           'alpha\nbeta\n'
                           '@intrinsic("return", @intrinsic("subtract", state\'s value, 2))\n')
    class_writer_main.write_text(class_writer_source)
    build(entry=class_writer_main)
    class_writer_main.write_text(class_writer_source.replace('"alpha"))', '"changed"))'))
    class_writer_edit = build(entry=class_writer_main)
    assert class_writer_edit[0]["inference_instantiations_reused"] >= 1, "class global writer was re-inferred"
    assert class_writer_edit[1:] == build(False, entry=class_writer_main)[1:]

    pointer_main = root / "pointer_main.dl"
    pointer_source = ('integer means: @intrinsic("type", "int")\n'
                      'pointer to kind means: @intrinsic("add pointer depth", kind)\n'
                      'to alpha {pointer to integer:value}:\n'
                      '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
                      'to beta {pointer to integer:value}:\n'
                      '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
                      '@intrinsic("store", number, 1)\n'
                      'alpha @intrinsic("address of", number)\n'
                      'beta @intrinsic("address of", number)\n')
    pointer_main.write_text(pointer_source)
    build(entry=pointer_main)
    pointer_main.write_text(pointer_source.replace('"alpha"))', '"changed"))'))
    pointer_edit = build(entry=pointer_main)
    assert pointer_edit[0]["inference_instantiations_reused"] >= 1, "pointer argument function was re-inferred"
    assert pointer_edit[1:] == build(False, entry=pointer_main)[1:], "pointer argument edit differs from clean build"

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
