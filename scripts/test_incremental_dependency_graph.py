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
