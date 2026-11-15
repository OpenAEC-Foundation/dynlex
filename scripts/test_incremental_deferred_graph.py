"""Keep source-match dependencies when unchanged syntax remains deferred."""

import shutil


def compare_current_graph(root, main, build):
    from test_incremental_program_cache import read_program

    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    warm = read_program(root / "cache", main)["section_dependency_graph"]
    saved = root / "deferred-graph-saved-cache"
    (root / "cache").rename(saved)
    try:
        fresh = build(entry=main)
        assert fresh[1:] == edited[1:]
        clean = read_program(root / "cache", main)["section_dependency_graph"]
        assert warm == clean, "deferred source changed the dependency graph"
    finally:
        shutil.rmtree(root / "cache")
        saved.rename(root / "cache")
    return edited


def check_deferred_graph(root, build):
    from test_incremental_program_cache import read_program

    main = root / "deferred_graph.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to first: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
              'to second: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "second"))\n'
              'first\nsecond\n')

    def compare_current():
        return compare_current_graph(root, main, build)

    main.write_text(source)
    build(entry=main)
    initial = read_program(root / "cache", main)
    declarations = initial["all_function_projection"]["inactive"]
    second = next(path for path, value in declarations.items() if value["header"] == "second:")
    initial_matches = initial["section_dependency_graph"]["matched"]
    changed = source.replace('integer, "first"', 'integer, "changed"')
    main.write_text(changed)
    edited = compare_current()
    assert edited[2] == b"changed\nsecond\n"
    assert edited[0]["source_owner_units_retained"] > 0
    assert edited[0]["body_resolution_units_deferred"] > 0
    assert edited[0]["matched_dependency_facts_reused"] > 0
    assert edited[0]["matched_dependency_owners_demanded"] == 0
    retained = read_program(root / "cache", main)["section_dependency_graph"]["matched"]
    assert retained[second] == initial_matches[second]
    main.write_text(changed + 'to unused: @intrinsic("discard", 4)\n')
    compare_current()
    main.write_text(changed)
    compare_current()
    main.write_text(changed.replace('second\n', 'missing\n'))
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(changed)
    compare_current()
    check_selected_dependencies(root, build)
    check_deferred_producer_version(root, build)
    from test_incremental_constraint_graph import check_constraint_graph
    check_constraint_graph(root, build)


def check_deferred_producer_version(root, build):
    main = root / "deferred_producer_version.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to idle: @intrinsic("discard", 1)\n'
              'to active: @intrinsic("discard", 0)\nactive\n')
    main.write_text(source)
    build(entry=main)
    changed = source.replace('"discard", 1', '"discard", integer')
    main.write_text(changed)
    unused = build(entry=main)
    assert unused[0]["type_inference_reused"]
    assert unused[0]["objects_built"] == 0 and unused[0]["link_ms"] == 0
    main.write_text(changed.replace('"discard", 0', '"discard", 2'))
    edited = compare_current_graph(root, main, build)
    assert edited[0]["matched_dependency_owners_demanded"] == 1, \
        "a newer deferred match producer reused stale dependency facts"


def check_selected_dependencies(root, build):
    from test_incremental_program_cache import read_program

    main = root / "deferred_selected_dependencies.dl"
    argument = '@intrinsic("command line argument count")'
    source = ('integer means: @intrinsic("type", "int")\n'
              'boolean means: @intrinsic("type", "bool")\n'
              'to get choose {integer:value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              'to get choose {boolean:value}:\n'
              '    @intrinsic("return", @intrinsic("add", @intrinsic("cast", value, integer), 10))\n'
              'to dispatch value: @intrinsic("discard", @intrinsic("call", "libc", "printf", integer, "%d\\n", choose value))\n'
              f'dispatch {argument}\n')
    main.write_text(source)
    first = build(entry=main)
    assert first[1:] == build(False, entry=main)[1:]
    assert first[2] == b"1\n"
    initial = read_program(root / "cache", main)
    declarations = initial["all_function_projection"]["inactive"]
    selected = next(path for path, value in declarations.items() if value["header"] == "dispatch value:")
    right = next(path for path, value in declarations.items() if value["header"] == "choose {boolean:value}:")
    matched = initial["section_dependency_graph"]["matched"][selected]
    main.write_text(source.replace(f'dispatch {argument}', f'dispatch @intrinsic("cast", {argument}, boolean)'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"10\n"
    graph = read_program(root / "cache", main)["section_dependency_graph"]
    assert graph["matched"][selected] == matched
    assert any(right in edges for edges in graph["instantiations"][selected].values()), \
        "changed typed demand lost the newly selected dependency"
