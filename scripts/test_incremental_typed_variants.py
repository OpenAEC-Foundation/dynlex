"""Repeated edits retain dependencies of functions with compile-time arguments."""

from test_incremental_program_cache import read_program


def check_typed_variant_dependency_retention(root, build):
    main = root / "typed_variant_dependencies.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'type means: @intrinsic("type", "type")\n'
              'the bonus means: 1\n'
              'to get adjusted value as {type:destination}:\n'
              '    @intrinsic("return", @intrinsic("cast", @intrinsic("add", value, the bonus), destination))\n'
              'to report:\n'
              '    @intrinsic("store", input, @intrinsic("call", "libc", "abs", integer, 4))\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "printf", integer, "%d\\n", '
              'adjusted input as integer))\n'
              'to touch: @intrinsic("discard", 1)\n'
              'report\ntouch\n'
              '@intrinsic("discard", adjusted 5 as integer)\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"5\n"
    initial = read_program(root / "cache", main)
    typed_path = next(path for path, variants in initial["inference_summaries"].items()
                      if any(variant["compile_time_parameters"] for variant in variants.values()))
    typed_units = initial["section_dependency_graph"]["inference_units"][typed_path]
    assert len(typed_units) == 1 and "*" not in typed_units
    for value in (2, 3, 4):
        main.write_text(source.replace('"discard", 1)', f'"discard", {value})'))
        edited = build(entry=main)
        assert edited[0]["functions_ir_reused"] >= 2, "successive edits lost typed function IR reuse"
        assert edited[0]["constraint_probes_reused"] >= 1, "an unrelated edit invalidated the typed constraint"
        assert edited[0]["inference_units_reused"] >= 2, "unchanged concrete dependency units were captured again"
        retained = read_program(root / "cache", main)["section_dependency_graph"]["inference_units"][typed_path]
        assert retained == typed_units, "successive edits changed the retained type-argument unit"
        assert edited[1:] == build(False, entry=main)[1:]
    graph = read_program(root / "cache", main)["section_dependency_graph"]
    expected = {}
    for caller, dependencies in graph["forward"].items():
        for dependency in dependencies:
            expected.setdefault(dependency, set()).add(caller)
    assert {path: set(callers) for path, callers in graph["reverse"].items()} == expected
    main.write_text(main.read_text().replace('the bonus means: 1', 'the bonus means: 2'))
    changed = build(entry=main)
    assert changed[2] == b"6\n", "a retained typed variant lost its transitive dependency"
    assert changed[1:] == build(False, entry=main)[1:]
