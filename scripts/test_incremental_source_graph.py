"""Retain source-only dependency products until matches or inference require work."""

from test_incremental_deferred_graph import compare_current_graph
from test_incremental_program_cache import read_program


def check_source_only_graph_reuse(root, build):
    main = root / "source_only_graph.dl"
    source = ('to get identity {item}:\n'
              '    @intrinsic("return", item)\n'
              'to unused {item}: @intrinsic("discard", item)\n'
              'to get active {item}:\n'
              '    @intrinsic("return", @intrinsic("add", item, 1))\n'
              '@intrinsic("discard", active @intrinsic("command line argument count"))\n')
    main.write_text(source)
    build(entry=main)
    changed = source.replace('item, 1', 'item, 2')
    main.write_text(changed)
    edited = compare_current_graph(root, main, build)
    assert edited[0]["dependency_graph_source_only_sections_reused"] > 0, \
        "unchanged source-only graph sections were evaluated again"

    selected = changed.replace('@intrinsic("discard", item)', '@intrinsic("discard", identity item)')
    main.write_text(selected)
    inactive = build(entry=main)
    assert inactive[1:] == build(False, entry=main)[1:]
    assert inactive[0]["type_inference_reused"] and inactive[0]["dependency_graph_ms"] == 0
    # The unused producer can stay pending until an active edit demands a graph.
    published = selected.replace('item, 2', 'item, 3')
    main.write_text(published)
    compare_current_graph(root, main, build)
    program = read_program(root / "cache", main)
    declarations = program["all_function_projection"]["inactive"]
    unused = next(path for path, value in declarations.items() if "unused" in value["header"])
    identity = next(path for path, value in declarations.items() if "identity" in value["header"])
    assert identity in program["section_dependency_graph"]["forward"][unused], \
        "a changed source-only section retained its previous edges"

    activated = published + 'unused 5\n'
    main.write_text(activated)
    compare_current_graph(root, main, build)
    assert read_program(root / "cache", main)["section_dependency_graph"]["instantiations"][unused], \
        "a newly inferred section retained its source-only product"
    main.write_text(activated.replace('to get identity {item}:', 'to get renamed {item}:'))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(activated)
    compare_current_graph(root, main, build)
