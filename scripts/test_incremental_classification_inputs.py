"""Classification inputs preserve lexical source identity and binding changes."""


def check_classification_inputs(root, build):
    main = root / "classification_source_origin.dl"
    body = root / "classification_moved_body.dl"
    prefix = (
        'integer means: @intrinsic("type", "int")\n'
        'value means: 9\n'
        '[helper] {number} means: @intrinsic("add", number, 1)\n'
        'local function [helper] [value]:\n'
        '    replacement:\n'
        '        7\n'
        'function [outer]:\n'
        '    execute:\n'
    )
    moved = '        @intrinsic("return", helper value)\n'
    suffix = (
        '        import classification_moved_body.dl\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", outer))\n'
    )
    # Keep both files present in the masked source projection before the move.
    tail = '        @intrinsic("discard", 0)\n'
    body.write_text(tail)
    main.write_text(prefix + moved + suffix)
    assert build(entry=main)[2] == b"7\n"
    body.write_text(moved + tail)
    main.write_text(prefix + suffix)
    changed = build(entry=main)
    clean = build(False, entry=main)
    assert clean[2] == b"10\n"
    assert changed[1:] == clean[1:], "moving a body across files retained a file-local match"
    body.write_text(tail)
    main.write_text(prefix + moved + suffix)
    restored = build(entry=main)
    assert restored[2] == b"7\n"
    assert restored[1:] == build(False, entry=main)[1:]
    check_reference_only_inputs(root, build)


def check_reference_only_inputs(root, build):
    main = root / "classification_reference_inputs.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [adjust] value:\n'
        '    execute:\n'
        '        @intrinsic("discard", value)\n'
        '        @intrinsic("return", value)\n'
        'function [wrapper] item:\n'
        '    execute:\n'
        '        @intrinsic("return", adjust item)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", wrapper 7))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    statement = '        @intrinsic("discard", 0)\n'
    inserted = source.replace('        @intrinsic("discard", value)\n', statement + '        @intrinsic("discard", value)\n')
    shifted = source.replace('@intrinsic("discard", value)',
                             '@intrinsic("discard", @intrinsic("add", 10000, value))')
    for current in (inserted, inserted.replace('"discard", 0', '"discard", 10000'), shifted, source):
        main.write_text(current)
        edited = build(entry=main)
        assert edited[0]["classification_units_invalidated"] == 0, \
            "a statement without references invalidated declaration classification"
        assert edited[0]["classification_parameters_reused"] >= 2
        assert edited[0]["classification_header_only_units_reused"] == 1
        assert edited[1:] == build(False, entry=main)[1:]

    # A new binding still matters even when its value has no pattern references.
    main.write_text(source.replace('        @intrinsic("discard", value)\n',
                                   '        @intrinsic("store", {spare}, 0)\n'
                                   '        @intrinsic("discard", value)\n'))
    binding = build(entry=main)
    assert binding[0]["classification_units_invalidated"] >= 1
    assert binding[1:] == build(False, entry=main)[1:]

    # Removing the last use changes the exported parameter and must reach callers.
    main.write_text(source.replace('@intrinsic("discard", value)', '@intrinsic("discard", 0)')
                   .replace('@intrinsic("return", value)', '@intrinsic("return", 0)'))
    removed = build(entry=main, success=False)
    assert removed[0]["classification_units_invalidated"] >= 1
    assert removed[1:] == build(False, entry=main, success=False)[1:]
