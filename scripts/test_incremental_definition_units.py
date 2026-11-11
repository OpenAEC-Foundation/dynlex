"""Reuse resolution units independently of whether definitions emit native code."""


from test_incremental_program_cache import read_program


def check_definition_resolution_units(root, build):
    main = root / "flex_resolution_units.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        '[twice] {integer:value} means: @intrinsic("add", value, value)\n'
        'function [discard] [twice] {integer:input}:\n'
        '    execute:\n'
        '        @intrinsic("discard", twice input)\n'
        'discard twice 7\n'
        '@intrinsic("discard", 1)\n'
    )
    main.write_text(source)
    cold = build(entry=main)
    for value in (2, 3):
        main.write_text(source.replace('"discard", 1)', f'"discard", {value})'))
        edited = build(entry=main)
        assert edited[0]["body_resolution_references_restored"] >= 3, "flex references bypassed resolution-unit reuse"
        assert edited[0]["body_resolution_reference_visits"] == 0, "unchanged flex bodies repeated resolution"
        assert edited[0]["body_resolution_reference_visits"] < cold[0]["body_resolution_reference_visits"]
        assert edited[1:] == build(False, entry=main)[1:]

    # Removing the last implicit-parameter use changes a flex's signature and
    # invalidates its caller, including a preceding failed literal lookup.
    source = ('integer means: @intrinsic("type", "int")\n'
              '[choose] value means: @intrinsic("add", value, 1)\n'
              'to [run] [choice]: @intrinsic("discard", choose 7)\nrun choice\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"add", value, 1', '"add", 1, 1'))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:], "cached flex classification hid a changed signature"
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "section_resolution_units.dl"
    source = (
        'flex section [announce] {message}:\n'
        '    replacement:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), message))\n'
        'announce "hello":\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), "body"))\n'
        '@intrinsic("discard", 1)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"hello\nbody\n"
    main.write_text(source.replace('"discard", 1)', '"discard", 2)'))
    edited = build(entry=main)
    assert edited[0]["body_resolution_references_restored"] >= 1, "section definition repeated resolution"
    assert edited[0]["body_resolution_reference_visits"] == 0
    assert edited[1:] == build(False, entry=main)[1:]

    main = root / "class_resolution_units.dl"
    source = (
        'class [managed]:\n'
        '    retain:\n'
        '        @intrinsic("discard", 0)\n'
        '    release:\n'
        '        @intrinsic("store", message, "cleanup")\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), message))\n'
        '@intrinsic("store", instance, @intrinsic("construct", managed))\n'
        '@intrinsic("discard", 1)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"cleanup\n"
    main.write_text(source.replace('"discard", 1)', '"discard", 2)'))
    edited = build(entry=main)
    assert edited[0]["body_resolution_references_restored"] >= 2, "class lifecycle repeated resolution"
    assert edited[0]["body_resolution_reference_visits"] == 0
    assert edited[1:] == build(False, entry=main)[1:]

    main = root / "flex_negative_lookup.dl"
    source = (
        '[identity] {value} means: value\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%d\\n", identity 7))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    main.write_text(source + 'value means: 9\n')
    changed = build(entry=main)
    assert changed[0]["body_resolution_references_restored"] == 0, "new literal reused a failed flex lookup"
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[2] == b"9\n"
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "flex_root_binding.dl"
    source = (
        '@intrinsic("discard", 0)\n'
        '[some] [value] means: 3\n'
        '[retrieve] means: some value\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, "%d\\n", retrieve))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"3\n"
    changed = source.replace('@intrinsic("discard", 0)', '@intrinsic("store", {some value}, 7)')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[0]["body_resolution_references_restored"] == 0, "root declaration reused an earlier flex binding"
    assert edited[2] == b"7\n"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(changed.replace('{some value}, 7)', '{some value}, 8)'))
    edited = build(entry=main)
    assert edited[0]["body_resolution_references_restored"] >= 1, "unchanged root binding prevented flex replay"
    assert edited[2] == b"8\n"
    assert edited[1:] == build(False, entry=main)[1:]

    main = root / "recursive_flex_classification.dl"
    source = (
        'flex function [alpha] value:\n'
        '    replacement:\n'
        '        @intrinsic("discard", value)\n'
        '        beta value\n'
        'flex function [beta] value:\n'
        '    replacement:\n'
        '        alpha value\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    build(entry=main)
    projection = read_program(root / "cache", main)["all_function_projection"]["matcher_projection"]
    before = projection["declaration_digests"]
    assert len(projection["body_promoted_parameters"]) == 2
    main.write_text(source.replace('        @intrinsic("discard", value)\n', ''))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    projection = read_program(root / "cache", main)["all_function_projection"]["matcher_projection"]
    after = projection["declaration_digests"]
    assert not projection["body_promoted_parameters"]
    assert len(before) == len(after) == 2
    assert all(after[path] != signature for path, signature in before.items()), "flex cycle kept a removed parameter"
    main.write_text(main.read_text() + 'alpha 7\n')
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
