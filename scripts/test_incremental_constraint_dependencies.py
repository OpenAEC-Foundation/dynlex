"""Constraint reuse follows possible complete pattern paths and actual reads."""


from test_incremental_constraint_units import (check_constraint_query_units, check_constraint_signature_dependencies,
                                              check_constraint_declaration_edits, check_constraint_provider_membership,
                                              check_constraint_provider_visibility, check_unbound_constraint_argument)


def check_constraint_lookup_dependencies(root, build):
    check_constraint_query_units(root, build)
    check_constraint_signature_dependencies(root, build)
    check_constraint_declaration_edits(root, build)
    check_constraint_provider_membership(root, build)
    check_constraint_provider_visibility(root, build)
    check_unbound_constraint_argument(root, build)
    check_constraint_body_dependencies(root, build)
    main = root / "constraint_lookup_dependencies.dl"
    for spelling, call in (
        ("{left} [plus] {right}", "1 plus 2"),
        ("{left} [in] {right}", "1 in 2"),
        ("[integer] [marker] {value}", "integer marker 1"),
        ("+ {value}", "+ (1)"),
        ("{left} [weave|braid] {right}", "1 braid 2"),
        ("{left} ≤ {right}", "1 ≤ 2"),
    ):
        source = (
            'integer means: @intrinsic("type", "int")\n'
            f'function {spelling}:\n'
            '    execute:\n'
            '        @intrinsic("return", 0)\n'
            'to consume {integer:value}: @intrinsic("discard", value)\n'
            'consume 1\n'
            '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
            f'({call})))\n'
        )
        main.write_text(source)
        assert build(entry=main)[2] == b"0\n"
        main.write_text(source.replace('"return", 0', '"return", 1'))
        edited = build(entry=main)
        assert edited[2] == b"1\n"
        assert edited[0]["constraint_probes_reused"] >= 1, \
            f"an unrelated pattern body re-probed integer: {spelling}"
        assert edited[0]["constraint_probes_run"] == 0
        assert edited[0]["constraint_values_reused"] >= 1
        assert edited[1:] == build(False, entry=main)[1:]

    source = (
        'function {left} [plus] {right}:\n'
        '    execute:\n'
        '        @intrinsic("return", @intrinsic("type", "int"))\n'
        'to consume {1 plus 2:value}: @intrinsic("discard", value)\n'
        'consume 1\n'
    )
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('"type", "int"', '"type", "float"'))
    affected = build(entry=main, success=False)
    assert affected[0]["constraint_probes_run"] >= 1, "a changed type provider retained its old result"
    assert affected[1:] == build(False, entry=main, success=False)[1:]

    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to consume {integer:value}: @intrinsic("discard", value)\n'
        'consume 1\n'
    )
    main.write_text(source)
    build(entry=main)
    main.write_text(source + 'function {left} [weave] {right}:\n    execute:\n'
                    '        @intrinsic("return", @intrinsic("variadic call", "libc", "printf", '
                    'integer, 1, "weave%d", left))\n')
    added = build(entry=main)
    assert added[0]["constraint_probes_reused"] >= 1, "an unrelated leading capture invalidated type constraints"
    assert added[0]["constraint_probes_run"] == 0
    assert added[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + '@intrinsic("discard", 1 weave 2)\n')
    called = build(entry=main)
    assert called[2] == b"weave1"
    assert called[1:] == build(False, entry=main)[1:]


def check_constraint_body_dependencies(root, build):
    main = root / "constraint_body_dependencies.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'floating-point number means: @intrinsic("type", "float")\n'
        'function {left} - {right}:\n'
        '    execute:\n'
        '        @intrinsic("return", 0)\n'
        'to consume {floating-point number:value}: @intrinsic("discard", value)\n'
        'consume 1.0\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", (4 - 2)))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"0\n"
    main.write_text(source.replace('"return", 0', '"return", 1'))
    edited = build(entry=main)
    assert edited[2] == b"1\n"
    assert edited[0]["constraint_probes_reused"] >= 1, "a punctuation substring created a false type dependency"
    assert edited[0]["constraint_probes_run"] == 0
    assert edited[1:] == build(False, entry=main)[1:]
