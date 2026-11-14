"""Reuse resolution work in active bodies and invalidate parameter discovery."""

from test_incremental_program_cache import read_program


def check_active_resolution_units(root, build):
    main = root / "active_resolution_units.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'left + right means: @intrinsic("add", left, right)\n'
        'function report number {integer:seed}:\n'
        '    globals:\n        amount\n'
        '    execute:\n'
        '        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", seed + amount))\n'
        'to touch: @intrinsic("discard", 1)\n'
        '@intrinsic("store", amount, 7)\n'
        'report number 5\ntouch\n'
    )
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"12\n"
    assert cold[0]["parameter_count_cache_ms"] >= 0
    for value in (2, 3):
        main.write_text(source.replace('"discard", 1)', f'"discard", {value})'))
        edited = build(entry=main)
        assert edited[0]["cached_body_resolution_references_skipped"] >= 2, "active global reader repeated early resolution"
        assert edited[0]["body_resolution_reference_visits"] < cold[0]["body_resolution_reference_visits"]
        assert edited[0]["matcher_declarations_reused"] > 0, "unchanged matcher declarations were serialized again"
        assert edited[0]["variable_like_count_units_reused"] > 0, "unchanged parameter discovery counts were recomputed"
        assert edited[0]["variable_like_count_references_scanned"] < cold[0]["variable_like_count_references_scanned"]
        assert edited[0]["variable_like_count_references_scanned"] == 0, "body replay rescanned unchanged parameter counts"
        assert edited[1:] == build(False, entry=main)[1:]

    main.write_text(main.read_text().replace('report number 5\ntouch\n', 'report number 6\ntouch\n'))
    top_level = build(entry=main)
    assert top_level[0]["matcher_declarations_reused"] > 0
    assert top_level[0]["variable_like_count_units_reused"] > 0
    assert top_level[0]["variable_like_count_references_scanned"] == 0
    assert top_level[1:] == build(False, entry=main)[1:]
    assert top_level[2] == b"13\n"

    main = root / "parameter_count_cascade.dl"
    source = 'to use value: @intrinsic("discard", value)\nto caller action: use 1\ncaller action\n'
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", value)', '"discard", 0)'))
    changed = build(entry=main, success=False)
    assert changed[0]["variable_like_count_units_reused"] > 0
    assert changed[1:] == build(False, entry=main, success=False)[1:], "last parameter use removal retained a caller match"
    main.write_text(main.read_text().replace('to caller action: use 1', 'to caller action: @intrinsic("discard", 1)'))
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "nested_parameter_counts.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to outer value:\n'
        '    to inner {integer:value}: @intrinsic("discard", value)\n'
        '    @intrinsic("discard", 0)\n'
        'to live: @intrinsic("discard", 1)\nlive\n'
    )
    main.write_text(source)
    build(entry=main)
    before = read_program(root / "cache", main)["all_function_projection"]
    counts = next(counts for counts in before["initial_parameter_counts"].values() if "outer" in counts)
    count = counts["value"]
    assert count > 0
    main.write_text(source.replace('"discard", value)', '"discard", 0)'))
    nested = build(entry=main)
    assert nested[1:] == build(False, entry=main)[1:]
    after = read_program(root / "cache", main)["all_function_projection"]
    counts = next(counts for counts in after["initial_parameter_counts"].values() if "outer" in counts)
    assert counts["value"] < count, "nested edit retained its ancestor's counts"


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
    before = read_program(root / "cache", main)["all_function_projection"]
    main.write_text('to use thing:\n' + literal_body + caller)
    edited = build(entry=main)
    after = read_program(root / "cache", main)["all_function_projection"]
    assert edited[0]["declaration_projection_units_built"] == 1, \
        "implicit parameter reclassification did not isolate its changed declaration"
    assert edited[0]["declaration_records_built"] == 1, \
        "the reclassified declaration record was not rebuilt alone"
    assert edited[0]["declaration_records_reused"] >= 1, \
        "an unchanged caller declaration was rebuilt with its changed matches"
    assert edited[0]["variable_like_count_units_reused"] >= 1, "unchanged wrapper recomputed its parameter counts"
    assert edited[0]["variable_like_count_references_scanned"] == 0, "parameter removal rescanned an unchanged body"
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
    nested_before = read_program(root / "cache", nested)["all_function_projection"]
    nested.write_text(nested_source.replace('"discard", thing', '"discard", 1'))
    nested_edit = build(entry=nested)
    nested_after = read_program(root / "cache", nested)["all_function_projection"]
    assert nested_before["declarations"] != nested_after["declarations"], \
        "nested body reference did not affect its ancestor's implicit parameter"
    assert nested_edit[1:] == build(False, entry=nested)[1:]
