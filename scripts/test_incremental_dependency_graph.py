"""Focused cross-file dependency graph cases for incremental native builds."""

import json


def graph_for(cache, entry):
    matches = []
    for path in (cache / "native-v1").glob("program-*.entry"):
        program = json.loads(path.read_text().split("\n", 2)[2])
        if str(entry) in program["files"]:
            matches.append(program["section_dependency_graph"])
    assert len(matches) == 1, "expected one cached program for the dependency fixture"
    return matches[0]


def projection_for(cache, entry):
    matches = []
    for path in (cache / "native-v1").glob("program-*.entry"):
        program = json.loads(path.read_text().split("\n", 2)[2])
        if str(entry) in program["files"]:
            matches.append(program["all_function_projection"])
    assert len(matches) == 1, "expected one cached program projection"
    return matches[0]


def assert_reverse_edges(graph):
    expected = {}
    for caller, dependencies in graph["forward"].items():
        for dependency in dependencies:
            expected.setdefault(dependency, set()).add(caller)
    actual = {dependency: set(callers) for dependency, callers in graph["reverse"].items()}
    assert actual == expected, "saved reverse edges disagree with saved forward edges"


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
    assert edited[0]["projection_global_matches_reused"] >= 1, \
        "unchanged global-reading function rebuilt its match projection after a value edit"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(definition + '@intrinsic("store", other, 2)\n' +
                    '@intrinsic("return", @intrinsic("subtract", read global, 2))\n')
    missing_global = build(entry=main, success=False)
    assert missing_global[1:] == build(False, entry=main, success=False)[1:], \
        "a removed global declaration retained its prior function match"


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
