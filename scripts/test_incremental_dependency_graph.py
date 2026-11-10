"""Focused cross-file dependency graph cases for incremental native builds."""

import json
from test_incremental_program_cache import read_program


def graph_for(cache, entry):
    return read_program(cache, entry)["section_dependency_graph"]


def projection_for(cache, entry):
    return read_program(cache, entry)["all_function_projection"]


def inactive_for(cache, entry):
    return read_program(cache, entry)["inactive_projection"]["inactive"]


def assert_reverse_edges(graph):
    expected = {}
    for caller, dependencies in graph["forward"].items():
        for dependency in dependencies:
            expected.setdefault(dependency, set()).add(caller)
    actual = {dependency: set(callers) for dependency, callers in graph["reverse"].items()}
    assert actual == expected, "saved reverse edges disagree with saved forward edges"


def check_cross_file_typed_overload(root, build):
    caller = root / "typed_caller.dl"
    overloads = root / "typed_overloads.dl"
    main = root / "typed_main.dl"
    caller.write_text('integer means: @intrinsic("type", "int")\nto emit:\n    use 1\n')
    generic = ('to use {value}: @intrinsic("discard", '
               '@intrinsic("call", "libc", "puts", integer, "generic"))\n')
    typed = ('to use {integer:value}: @intrinsic("discard", '
             '@intrinsic("call", "libc", "puts", integer, "typed"))\n')
    overloads.write_text(generic)
    main.write_text('import typed_caller.dl\nimport typed_overloads.dl\nemit\n')
    initial = build(entry=main)
    assert initial[2] == b"generic\n"
    assert initial[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + '@intrinsic("discard", 1)\n')
    execution_edit = build(entry=main)
    assert execution_edit[0]["projection_top_level_sections_reused"] >= 1
    assert execution_edit[1:] == build(False, entry=main)[1:]
    overloads.write_text(generic + typed)
    added = build(entry=main)
    assert added[2] == b"typed\n", "new cross-file overload did not invalidate its caller"
    assert added[1:] == build(False, entry=main)[1:]
    overloads.write_text(generic)
    removed = build(entry=main)
    assert removed[2] == b"generic\n", "removed cross-file overload remained selected"
    assert removed[1:] == build(False, entry=main)[1:]


def check_reused_ir_dependency_graph_variants(root, build):
    main = root / "reused_ir_graph.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to leaf: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "leaf"))\n'
              'to caller: leaf\ncaller\n@intrinsic("discard", 1)\n')
    main.write_text(source)
    build(entry=main)
    projection = projection_for(root / "cache", main)
    leaf_path = next(path for path, declaration in projection["function_declarations"].items()
                     if declaration["identity"]["header"] == "leaf:")
    prior = graph_for(root / "cache", main)
    assert prior["instantiations"][leaf_path]
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["functions_ir_reused"] >= 2
    current = graph_for(root / "cache", main)
    assert current["instantiations"][leaf_path] == prior["instantiations"][leaf_path], \
        "reused IR lost its inferred dependency variants"
    assert_reverse_edges(current)
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace('"leaf"', '"changed"'))
    changed = build(entry=main)
    assert changed[2] == b"changed\n"
    assert changed[1:] == build(False, entry=main)[1:]


def check_leaf_dependency_cascade(root, build):
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
    leaf_a.write_text('# comment between executable builds\n' + leaf_a.read_text())
    assert build(entry=leaf_main)[0]["frontend_reused"]
    leaf_a.write_text(leaf_a.read_text().replace('"first"', '"changed"'))
    leaf_edit = build(entry=leaf_main)
    assert leaf_edit[0]["source_projections_reused"] >= 3, "unchanged imported files were reprojected"
    assert leaf_edit[0]["preinfer_dependency_graph_sections_checked"] >= 2, \
        "preinference traversed unchanged function bodies instead of saved dependencies"
    assert leaf_edit[0]["inference_summary_sections_reused"] >= 2, \
        "cached function inference summaries were re-encoded"
    assert leaf_edit[0]["dependency_graph_sections_reused"] >= 3, "unchanged sections ignored saved dependencies"
    assert leaf_edit[0]["dependency_graph_entries_reused"] >= 2, "reused inference dependencies were captured again"
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
    leaf_b.write_text(leaf_b.read_text() + 'to unused addition:\n    @intrinsic("discard", 3)\n')
    added_definition = build(entry=leaf_main)
    assert added_definition[0]["inference_instantiations_reused"] >= 3, f"unrelated definition re-inferred existing functions: {added_definition[0]}"
    assert added_definition[0]["functions_ir_reused"] >= 3, "unrelated definition regenerated existing function IR"
    assert added_definition[1:] == build(False, entry=leaf_main)[1:], "unrelated definition changed incremental build behavior"
    leaf_a.write_text('to unused prefix:\n    @intrinsic("discard", 4)\n' + leaf_a.read_text())
    inserted_definition = build(entry=leaf_main)
    assert inserted_definition[0]["inference_instantiations_reused"] >= 3, f"insertion shifted unchanged function identities: {inserted_definition[0]}"
    assert inserted_definition[0]["functions_ir_reused"] >= 3, "insertion regenerated unchanged function IR"
    assert inserted_definition[1:] == build(False, entry=leaf_main)[1:], "inserted definition changed incremental build behavior"

    reverse_main = root / "reverse_edges.dl"
    reverse_source = 'to leaf: @intrinsic("discard", 1)\nto caller: leaf\ncaller\n'
    reverse_main.write_text(reverse_source)
    build(entry=reverse_main)
    assert_reverse_edges(graph_for(root / "cache", reverse_main))
    reverse_main.write_text(reverse_source.replace('to caller: leaf', 'to caller: @intrinsic("discard", 2)'))
    removed_call = build(entry=reverse_main)
    assert removed_call[0]["dependency_graph_reverse_nodes_updated"] >= 1, "removed call kept its reverse edge"
    assert_reverse_edges(graph_for(root / "cache", reverse_main))
    assert removed_call[1:] == build(False, entry=reverse_main)[1:]
    reverse_main.write_text(reverse_main.read_text().replace('"discard", 1', '"discard", 3'))
    changed_leaf = build(entry=reverse_main)
    assert changed_leaf[0]["dependency_graph_reverse_nodes_updated"] == 0, "unchanged edges were rebuilt"
    assert_reverse_edges(graph_for(root / "cache", reverse_main))
    assert changed_leaf[1:] == build(False, entry=reverse_main)[1:]
    reverse_main.write_text(reverse_main.read_text().replace('to caller: @intrinsic("discard", 2)', 'to caller: leaf'))
    added_call = build(entry=reverse_main)
    assert added_call[0]["dependency_graph_reverse_nodes_updated"] >= 1, "added call lacked its reverse edge"
    assert_reverse_edges(graph_for(root / "cache", reverse_main))
    assert added_call[1:] == build(False, entry=reverse_main)[1:]

    variant_main = root / "variant_ir_reuse.dl"
    variant_definition = 'to consume {value}: @intrinsic("discard", value)\n'
    variant_main.write_text(variant_definition + 'consume 1\nconsume 1.0\n')
    build(entry=variant_main)
    variant_main.write_text(variant_definition + 'consume 1\n')
    build(entry=variant_main)
    integer_graph = graph_for(root / "cache", variant_main)
    consume_path = next(path for path, variants in integer_graph["instantiations"].items() if variants)
    integer_keys = set(integer_graph["instantiations"][consume_path])
    variant_main.write_text(variant_definition + 'consume 1.0\n')
    variant_edit = build(entry=variant_main)
    float_graph = graph_for(root / "cache", variant_main)
    assert set(float_graph["instantiations"][consume_path]) != integer_keys, \
        "dependency graph retained a specialization that was no longer instantiated"
    assert_reverse_edges(float_graph)
    assert variant_edit[1:] == build(False, entry=variant_main)[1:], \
        "cached inference reused a specialization whose LLVM function was removed"


def check_resolved_declaration_snapshot_reuse(root, build):
    main = root / "declaration_snapshot_reuse.dl"
    source = ('to active:\n'
              '    @intrinsic("discard", 1)\n'
              'to independent:\n'
              '    @intrinsic("discard", 3)\n'
              'active\nindependent\n')
    main.write_text(source)
    build(entry=main)
    prior = projection_for(root / "cache", main)
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    current = projection_for(root / "cache", main)
    assert edited[0]["projection_declaration_sections_reused"] >= 1, \
        "an active body edit reserialized an independent resolved declaration"
    assert current["declarations"] == prior["declarations"]
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + 'to addition: @intrinsic("discard", 4)\n')
    added = build(entry=main)
    assert added[0]["projection_declaration_sections_reused"] == 0, \
        "a new declaration reused the old whole-program declaration snapshot"
    assert added[1:] == build(False, entry=main)[1:]


def check_inactive_projection_refresh(root, build):
    main = root / "inactive_projection_refresh.dl"
    integer = 'integer means: @intrinsic("type", "int")\n'
    live = 'to live: @intrinsic("discard", 1)\n'
    messages = ('to old message: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "old"))\n'
                'to new message: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "new"))\n')
    old = 'to spare: old message\n'
    new = 'to spare: new message\n'
    main.write_text(integer + live + messages + old + 'live\n')
    build(entry=main)
    before = projection_for(root / "cache", main)
    main.write_text(integer + live + messages + new + 'live\n')
    edited = build(entry=main)
    assert edited[0]["type_inference_reused"], "inactive body edit reran type inference"
    assert edited[0]["inactive_projection_matches_rechecked"] == 1, \
        "inactive projection rematched unchanged function bodies"
    after = projection_for(root / "cache", main)
    changed = [path for path in after["bodies"] if after["bodies"][path] != before["bodies"][path]]
    assert len(changed) == 1 and after["matches"][changed[0]] != before["matches"][changed[0]], \
        "inactive body edit kept a stale body or match fingerprint"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(integer + live + messages + new + 'live\nspare\n')
    called = build(entry=main)
    assert called[1:] == build(False, entry=main)[1:], "later call used the prior inactive body"


def check_inactive_edit_with_unrelated_warning(root, build):
    helper = root / "warn_inactive_helper.dl"
    main = root / "warn_inactive_main.dl"
    helper.write_text('to unused: @intrinsic("discard", 1)\n')
    main.write_text('import warn_inactive_helper.dl\n'
                    'integer means: @intrinsic("type", "int")\n'
                    'to show value as the value: @intrinsic("discard", value)\n'
                    'show 1 as the value\n')
    cold = build(entry=main)
    assert "Warning:" in cold[1], "fixture did not produce a warning"
    helper.write_text('to unused: @intrinsic("discard", 2)\n')
    edited = build(entry=main)
    assert edited[0]["type_inference_reused"], "unrelated warning disabled inactive-body inference reuse"
    assert edited[0]["diagnostic_records_reused"] > 0, "unrelated warning was not carried forward"
    assert edited[1:] == build(False, entry=main)[1:], "inactive edit lost or changed an unrelated warning"
    helper.write_text('to unused: nonexistent call\n')
    invalid = build(entry=main, success=False)
    assert invalid[1] == build(False, entry=main, success=False)[1], \
        "a new diagnostic in the edited body was hidden by cached warnings"


def check_frontend_warning_inference_reuse(root, build):
    main = root / "frontend_warning_inference_reuse.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'value means: 1\n'
              'to show {integer:value}: @intrinsic("discard", value)\n'
              'show 2\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    first = build(entry=main)
    assert "Warning:" in first[1], "fixture did not produce a declaration warning"
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["inference_instantiations_reused"] >= 1, \
        "a declaration warning prevented reuse of an unchanged function"
    assert edited[1:] == build(False, entry=main)[1:]


def check_inference_warning_rechecked(root, build):
    main = root / "inference_warning_rechecked.dl"
    source = ('function return value:\n'
              '    replacement: @intrinsic("return", value)\n'
              'to get left - right:\n'
              '    return @intrinsic("subtract", left, right)\n'
              'to get value adjusted:\n'
              '    return value\n'
              'to get the tested form of value:\n'
              '    return value - 1 adjusted\n'
              '@intrinsic("discard", the tested form of 4)\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    first = build(entry=main)
    assert "multiple valid operand groupings" in first[1], "fixture did not produce an inference warning"
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:], "cached inference hid a warning"


def check_inactive_edit_after_warning_wording(root, build):
    folder = root / "warn_inactive_config"
    folder.mkdir()
    helper = folder / "helper.dl"
    main = folder / "main.dl"
    config = folder / "config.dl"
    helper.write_text('to unused: @intrinsic("discard", 1)\n')
    main.write_text('import helper.dl\n'
                    'integer means: @intrinsic("type", "int")\n'
                    'value means: 1\n'
                    'to show {integer:value}: @intrinsic("discard", value)\n'
                    'show 2\n')
    wording = lambda name: ('dynlex options:\n    messages:\n'
                            f'        explicit parameter ambiguity: "{name} warning for {{parameter}}"\n')
    config.write_text(wording("First"))
    first = build(entry=main)
    assert "First warning" in first[1]
    config.write_text(wording("Second"))
    changed_wording = build(entry=main)
    assert changed_wording[0]["frontend_reused"] and "Second warning" in changed_wording[1]
    helper.write_text('to unused: @intrinsic("discard", 2)\n')
    edited = build(entry=main)
    assert edited[0]["type_inference_reused"], "warning wording invalidated an unrelated inactive body"
    assert edited[1:] == build(False, entry=main)[1:], "edited build used old diagnostic wording"


def check_runtime_pure_value_summary(root, build):
    main = root / "runtime_pure_value_summary.dl"
    definition = 'function identity {integer:value}:\n    execute:\n        @intrinsic("return", value)\n'
    prefix = ('integer means: @intrinsic("type", "int")\n' + definition +
              '@intrinsic("discard", identity (@intrinsic("call", "libc", "rand", integer)))\n')
    main.write_text(prefix + '@intrinsic("discard", 1)\n')
    build(entry=main)
    main.write_text(prefix + '@intrinsic("discard", 2)\n')
    runtime_edit = build(entry=main)
    assert runtime_edit[0]["inference_instantiations_reused"] >= 1, \
        "runtime call re-inferred a pure value function with an unchanged body"
    assert runtime_edit[1:] == build(False, entry=main)[1:]
    main.write_text(prefix + '@intrinsic("return", @intrinsic("subtract", identity 4, 4))\n')
    known_call = build(entry=main)
    assert known_call[1:] == build(False, entry=main)[1:], \
        "a later known-value call used a cached summary without inferring its body"
    changed_definition = definition.replace('"return", value', '"return", @intrinsic("add", value, 1)')
    main.write_text((prefix + '@intrinsic("return", @intrinsic("subtract", identity 4, 4))\n')
                    .replace(definition, changed_definition)
                    .replace('identity 4, 4', 'identity 4, 5'))
    changed_body = build(entry=main)
    assert changed_body[1:] == build(False, entry=main)[1:], "changed pure body retained its prior summary"


def check_global_match_reuse_after_execution_edit(root, build):
    main = root / "global_match_reuse.dl"
    definition = ('function read global:\n'
                  '    globals:\n'
                  '        value\n'
                  '    execute:\n'
                  '        @intrinsic("return", value)\n')
    prefix = definition + '@intrinsic("store", value, 1)\n'
    main.write_text(prefix + '@intrinsic("return", @intrinsic("subtract", read global, 1))\n')
    build(entry=main)
    main.write_text(prefix.replace('value, 1', 'value, 2') +
                    '@intrinsic("return", @intrinsic("subtract", read global, 2))\n')
    edited = build(entry=main)
    assert edited[0]["projection_global_matches_reused"] + edited[0]["projection_top_level_sections_reused"] >= 1, \
        "unchanged global-reading function rebuilt its match projection after a value edit"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(definition + '@intrinsic("store", other, 2)\n' +
                    '@intrinsic("return", @intrinsic("subtract", read global, 2))\n')
    missing_global = build(entry=main, success=False)
    assert missing_global[1:] == build(False, entry=main, success=False)[1:], \
        "a removed global declaration retained its prior function match"


def check_top_level_projection_reuse(root, build):
    main = root / "top_level_projection_reuse.dl"
    declarations = ('integer means: @intrinsic("type", "int")\n'
                    'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "stable"))\n')
    execution = ('@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
                 'report\n')
    main.write_text(declarations + execution)
    build(entry=main)
    main.write_text(declarations + execution.replace('"first"', '"second"'))
    edited = build(entry=main)
    assert edited[0]["projection_top_level_sections_reused"] >= 1, \
        "top-level executable edit rebuilt unchanged function projections"
    assert edited[2] == b"second\nstable\n"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(declarations + execution.replace('"first"', '"third"'))
    edited_again = build(entry=main)
    assert edited_again[0]["projection_top_level_sections_reused"] >= 1
    assert edited_again[2] == b"third\nstable\n"
    assert edited_again[1:] == build(False, entry=main)[1:]
    main.write_text(declarations.replace('"stable"', '"updated"') + execution.replace('"first"', '"third"'))
    changed_function = build(entry=main)
    assert changed_function[0]["projection_top_level_sections_reused"] == 0, \
        "changed function body reused its old projection"
    assert changed_function[1:] == build(False, entry=main)[1:]


def check_known_pure_return_summary(root, build):
    main = root / "known_pure_return_summary.dl"
    definition = ('integer means: @intrinsic("type", "int")\n'
                  'function identity {integer:value}:\n'
                  '    execute:\n'
                  '        @intrinsic("return", value)\n')
    runtime = '@intrinsic("discard", identity (@intrinsic("call", "libc", "rand", integer)))\n'
    main.write_text(definition + runtime + '@intrinsic("return", @intrinsic("subtract", identity 4, 4))\n')
    build(entry=main)
    main.write_text(definition + runtime +
                    '@intrinsic("return", @intrinsic("subtract", identity 4, @intrinsic("add", 2, 2)))\n')
    reused = build(entry=main)
    assert reused[0]["pure_value_summary_hits"] >= 1, "known pure result re-inferred despite an identical argument"
    assert reused[1:] == build(False, entry=main)[1:]
    main.write_text(definition + runtime + '@intrinsic("return", @intrinsic("subtract", identity 5, 5))\n')
    new_argument = build(entry=main)
    assert new_argument[1:] == build(False, entry=main)[1:], "new pure argument used a stale cached result"
    changed_definition = definition.replace('"return", value', '"return", @intrinsic("add", value, 1)')
    main.write_text(changed_definition + runtime +
                    '@intrinsic("return", @intrinsic("subtract", identity 4, 5))\n')
    changed_body = build(entry=main)
    assert changed_body[1:] == build(False, entry=main)[1:], "changed pure body retained a cached result"


def check_folded_value_remains_active(root, build):
    main = root / "folded_value_remains_active.dl"
    declarations = ('integer means: @intrinsic("type", "int")\n'
                    'function greeting:\n'
                    '    execute:\n'
                    '        @intrinsic("return", "first")\n'
                    'function print greeting:\n'
                    '    execute:\n'
                    '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, greeting))\n')
    execution = 'print greeting\n@intrinsic("discard", 1)\n'
    main.write_text(declarations + execution)
    build(entry=main)
    before = inactive_for(root / "cache", main)
    main.write_text(declarations + execution.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    after = inactive_for(root / "cache", main)
    assert after == before, "a folded function became inactive when its caller reused IR"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text((declarations + execution.replace('"discard", 1', '"discard", 2'))
                    .replace('"first"', '"second"'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:], "folded value edit retained old caller output"


def check_local_match_projection_reuse(root, build):
    main = root / "local_match_projection.dl"
    prefix = 'to leaf: @intrinsic("discard", 1)\nto run:\n    leaf\n    leaf\n'
    main.write_text(prefix + '    @intrinsic("discard", 1)\nrun\n')
    build(entry=main)
    main.write_text(prefix + '    @intrinsic("discard", 2)\nrun\n')
    edited = build(entry=main)
    assert edited[0]["projection_match_entries_reused"] >= 2, \
        "a one-line body edit re-fingerprinted unchanged pattern calls"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(prefix.replace('to run:\n', 'to run:\n    @intrinsic("discard", 9)\n') +
                    '    @intrinsic("discard", 2)\nrun\n')
    inserted = build(entry=main)
    assert inserted[0]["projection_match_entries_reused"] >= 2, \
        "inserting a body line re-fingerprinted later unchanged calls"
    assert inserted[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + 'to unrelated: @intrinsic("discard", 3)\n')
    unrelated = build(entry=main)
    assert unrelated[1:] == build(False, entry=main)[1:], \
        "adding an unrelated definition changed the saved call matches"


def check_pointer_preinfer(root, build):
    main = root / "pointer_preinfer.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'pointer to kind means: @intrinsic("add pointer depth", kind)\n'
              'to alpha {pointer to integer:value}:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "alpha"))\n'
              'to beta {pointer to integer:value}:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "beta"))\n'
              '@intrinsic("store", number, 1)\n'
              'alpha @intrinsic("address of", number)\n'
              'beta @intrinsic("address of", number)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"alpha"))', '"changed"))'))
    edited = build(entry=main)
    assert edited[0]["preinferred_instantiations"] >= 1, "pointer argument function was not pre-inferred"
    assert edited[0]["inference_instantiations_reused"] >= 1, "independent pointer function was re-inferred"
    assert edited[1:] == build(False, entry=main)[1:]


def check_implicit_parameter_classification_cascade(root, build):
    main = root / "implicit_classification_cascade.dl"
    caller = ('to wrapper thing:\n'
              '    use thing\n'
              '    @intrinsic("discard", thing)\n'
              'wrapper 1\n')
    parameter_body = '    @intrinsic("discard", thing)\n'
    literal_body = '    @intrinsic("discard", 1)\n'
    main.write_text('to use thing:\n' + parameter_body + caller)
    build(entry=main)
    before = projection_for(root / "cache", main)
    main.write_text('to use thing:\n' + literal_body + caller)
    edited = build(entry=main)
    after = projection_for(root / "cache", main)
    assert edited[0]["projection_declaration_sections_reused"] == 0, \
        "implicit parameter reclassification reused a stale declaration snapshot"
    assert edited[0]["variable_like_count_references_scanned"] >= 2
    assert before["declarations"] != after["declarations"], \
        "implicit parameter removal kept the old declaration dependency"
    changed = [path for path in before["function_declarations"]
               if before["function_declarations"][path] != after["function_declarations"][path]]
    assert len(changed) == 1, "body edit did not isolate the reclassified declaration"
    caller_path = next(path for path, declaration in before["function_declarations"].items()
                       if declaration["identity"]["header"] == "wrapper thing:")
    assert before["matches"][caller_path] != after["matches"][caller_path], \
        "caller matches ignored reclassified pattern"
    assert edited[1:] == build(False, entry=main)[1:]

    nested = root / "nested_body_implicit_classification.dl"
    nested_source = ('function use thing:\n'
                     '    execute:\n'
                     '        @intrinsic("discard", thing)\n')
    nested.write_text(nested_source)
    build(entry=nested)
    nested_before = projection_for(root / "cache", nested)
    nested.write_text(nested_source.replace('"discard", thing', '"discard", 1'))
    nested_edit = build(entry=nested)
    nested_after = projection_for(root / "cache", nested)
    assert nested_before["declarations"] != nested_after["declarations"], \
        "nested body reference did not affect its ancestor's implicit parameter"
    assert nested_edit[1:] == build(False, entry=nested)[1:]


def check_independent_body_resolution_order(root, build):
    main = root / "independent_body_resolution.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to alpha {integer:value}:\n'
              '    use value with later\n'
              'to use value with later:\n'
              '    @intrinsic("discard", value)\n'
              'alpha 1\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[0]["body_resolution_reference_visits"] == 2, \
        "an independent body was matched before its callees were indexed"
    main.write_text(source.replace('alpha 1\n', 'alpha 2\n'))
    edited = build(entry=main)
    assert edited[0]["body_resolution_reference_visits"] == 2
    assert edited[1:] == build(False, entry=main)[1:]

    alternative = root / "single_word_alternative_promotion.dl"
    alternative.write_text('function [the negative of |-]value:\n'
                           '    execute:\n'
                           '        @intrinsic("return", @intrinsic("negate", value))\n'
                           '@intrinsic("discard", the negative of 1)\n')
    promoted = build(entry=alternative)
    assert promoted[1:] == build(False, entry=alternative)[1:], \
        "single-word alternative lost its body-driven parameter promotion"


def check_independent_reference_delay(root, build):
    main = root / "independent_reference_delay.dl"
    source = ('to use value:\n'
              '    helper\n'
              '    @intrinsic("discard", value)\n'
              'to helper:\n'
              '    @intrinsic("discard", 1)\n'
              'use 2\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["deferred_match_keys_replayed"] >= 1, \
        "independent body reference waited for unrelated parameter classification"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace('use 2\n', 'use 3\n'))
    top_level = build(entry=main)
    assert top_level[0]["deferred_match_keys_replayed"] >= 1, \
        "top-level execution invalidated a function without global access"
    assert top_level[1:] == build(False, entry=main)[1:]

    overlap = root / "candidate_substring_delay.dl"
    overlap_source = ('to use it:\n'
                      '    @intrinsic("discard", print)\n'
                      'print means: 1\n'
                      'to helper:\n'
                      '    @intrinsic("discard", 1)\n'
                      'use it\n')
    overlap.write_text(overlap_source)
    build(entry=overlap)
    overlap.write_text(overlap_source.replace('"discard", 1', '"discard", 2'))
    overlap_edit = build(entry=overlap)
    assert overlap_edit[0]["deferred_match_keys_replayed"] >= 1, \
        "a substring-only candidate delayed an unrelated body match"
    assert overlap_edit[1:] == build(False, entry=overlap)[1:]


def check_generated_accessor_graph_edges(root, build):
    main = root / "generated_accessor_graph.dl"
    main.write_text('integer means: @intrinsic("type", "int")\n'
                    'function left plus right:\n'
                    '    after: default\n'
                    '    replacement: @intrinsic("add", left, right)\n'
                    'function left times right:\n'
                    '    before: default\n'
                    '    replacement: @intrinsic("multiply", left, right)\n'
                    'class box:\n'
                    '    members:\n'
                    '        x as integer\n'
                    '@intrinsic("return", 0)\n')
    compiled = build(entry=main)
    assert compiled[0]["precedence_graph_edges"] <= 8, \
        "generated accessors created an edge for every accessor and precedence target"
    assert compiled[1:] == build(False, entry=main)[1:]
    no_accessors = root / "precedence_graph_without_accessors.dl"
    no_accessors.write_text(main.read_text().replace('class box:\n    members:\n        x as integer\n', ''))
    bare = build(entry=no_accessors)
    assert bare[0]["precedence_graph_edges"] == 2, \
        "the accessor sentinel added edges when the program has no generated accessors"


def check_global_reader_after_top_level_edit(root, build):
    main = root / "global_reader_top_level_edit.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              '@intrinsic("store", counter, 1)\n'
              'function read state:\n'
              '    globals:\n'
              '        counter\n'
              '    execute:\n'
              '        @intrinsic("return", counter)\n'
              '@intrinsic("return", @intrinsic("subtract", read state, 1))\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('counter, 1)', 'counter, 2)').replace('read state, 1)', 'read state, 2)'))
    edited = build(entry=main)
    assert edited[0]["inference_instantiations_reused"] >= 1, \
        "top-level value edit re-inferred a function with unchanged global types"
    assert edited[0]["functions_ir_reused"] >= 1, "top-level value edit regenerated an unchanged global reader"
    assert edited[1:] == build(False, entry=main)[1:]

    typed = root / "global_reader_type_edit.dl"
    typed_source = source.replace('function read state:', 'function inspect state:').replace(
        '@intrinsic("return", counter)', '@intrinsic("discard", counter)').replace(
        '@intrinsic("return", @intrinsic("subtract", read state, 1))', 'inspect state')
    typed.write_text(typed_source)
    build(entry=typed)
    typed.write_text(typed_source.replace('counter, 1)', 'counter, 1.0)'))
    changed_type = build(entry=typed)
    assert changed_type[0]["inference_instantiations_reused"] == 0, \
        "global type change reused an old inference summary"
    assert changed_type[1:] == build(False, entry=typed)[1:]


def check_deferred_match_key_replay(root, build):
    main = root / "deferred_match_key_replay.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to first: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
              'to caller: first\n'
              'to independent: @intrinsic("discard", 3)\n'
              'caller\n')
    main.write_text(source)
    build(entry=main)
    assert projection_for(root / "cache", main)["initial_matcher_tree"], \
        "the indexed pattern tree signature was not persisted"
    main.write_text(source.replace('"discard", 3', '"discard", 4'))
    edited = build(entry=main)
    assert edited[0]["deferred_match_keys_replayed"] >= 1, \
        "unchanged body matched after definitions were indexed instead of replaying its resolved reference"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace('to independent:', 'to renamed independent:'))
    renamed = build(entry=main)
    assert renamed[0]["deferred_match_keys_replayed"] == 0, \
        "declaration edit replayed matches against a different pattern tree"
    assert renamed[0]["precedence_match_keys_replayed"] == 0, \
        "declaration edit replayed precedence matches against a different pattern tree"
    assert renamed[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace(
        'to renamed independent:',
        'to first: @intrinsic("discard", 5)\nto renamed independent:'))
    changed_tree = build(entry=main, success=False)
    assert changed_tree[1] == build(False, entry=main, success=False)[1]


def check_precedence_match_key_replay(root, build):
    main = root / "precedence_key_replay.dl"
    source = ('function left plus right:\n'
              '    before: $ times $\n'
              '    replacement: @intrinsic("add", left, right)\n'
              'function left times right:\n'
              '    replacement: @intrinsic("multiply", left, right)\n'
              'to keep grouping: @intrinsic("discard", 1 plus 2 times 3)\n'
              'to unrelated: @intrinsic("discard", 4)\n'
              '@intrinsic("return", 0)\n')
    main.write_text(source)
    build(entry=main)
    projection = projection_for(root / "cache", main)
    assert projection["final_matcher_tree"] and projection["precedence_signature"], \
        "precedence matching environment was not persisted"
    main.write_text(source.replace('"discard", 4', '"discard", 5'))
    edited = build(entry=main)
    assert edited[0]["precedence_match_keys_replayed"] >= 1, \
        "unchanged precedence-sensitive function computed matcher keys again"
    assert edited[0]["precedence_signature_reused"] >= 1, \
        "unchanged precedence rules rebuilt their full signature"
    assert edited[0]["precedence_signature_ms"] == 0
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace('before: $ times $', 'after: $ times $'))
    changed_rule = build(entry=main)
    assert changed_rule[0]["precedence_match_keys_replayed"] == 0, \
        "changed precedence rule replayed a stale matcher key"
    assert changed_rule[0]["precedence_signature_reused"] == 0, \
        "changed precedence rule reused its prior signature"
    assert changed_rule[1:] == build(False, entry=main)[1:]
