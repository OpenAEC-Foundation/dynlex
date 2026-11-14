"""Propagate changed effects through callers and stop at unchanged interfaces."""

from test_incremental_program_cache import read_program


def summary(program, header):
    path = next(path for path, entry in program["all_function_projection"]["inactive"].items()
                if entry["header"] == header)
    variants = program["inference_summaries"][path]
    assert len(variants) == 1
    return next(iter(variants.values()))


def check_bounded_effect_cascade(root, build):
    main = root / "bounded_cascade.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to leaf:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "leaf"))\n'
        'to middle:\n'
        '    leaf\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "middle"))\n'
        'to outer: middle\nouter\n'
    )
    main.write_text(source)
    build(entry=main)
    before = read_program(root / "cache", main)
    leaf_statement = '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "leaf"))'
    main.write_text(source.replace(leaf_statement, '@intrinsic("discard", 1)'))
    leaf_edit = build(entry=main)
    assert leaf_edit[0]["inference_instantiations_reused"] >= 1
    assert leaf_edit[0]["functions_ir_reused"] >= 1, "effect change rebuilt the outer caller IR"
    assert leaf_edit[1:] == build(False, entry=main)[1:]
    leaf_program = read_program(root / "cache", main)
    assert summary(leaf_program, "middle:") == summary(before, "middle:")
    assert summary(leaf_program, "outer:") == summary(before, "outer:")

    middle_statement = '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "middle"))'
    main.write_text(main.read_text().replace(middle_statement, '@intrinsic("discard", 2)'))
    middle_edit = build(entry=main)
    assert middle_edit[1:] == build(False, entry=main)[1:]
    changed = read_program(root / "cache", main)
    assert summary(changed, "outer:")["purity"] != summary(leaf_program, "outer:")["purity"], \
        "changed middle effects left the outer inference summary stale"
    assert summary(changed, "outer:")["purity"] == summary(changed, "middle:")["purity"] == \
        summary(changed, "leaf:")["purity"]
    assert middle_edit[0]["inference_instantiations_reused"] >= 1, "unchanged leaf was inferred again"

    invalid = root / "invalid_cascade.dl"
    invalid.write_text(source)
    build(entry=invalid)
    invalid.write_text(source.replace(leaf_statement, '@intrinsic("add", 1, "x")'))
    assert build(success=False, entry=invalid)[1] == build(False, success=False, entry=invalid)[1], \
        "failed nested preinference changed diagnostics"
