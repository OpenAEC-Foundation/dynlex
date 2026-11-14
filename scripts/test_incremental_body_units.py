"""Restore unchanged body resolution, including failed-lookups-as-variables."""

def check_body_resolution_units(root, build):
    main = root / "body_units.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to alpha {message}: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'to beta {message}: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'to gamma {message}:\n'
              '    @intrinsic("store", local, message)\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, local))\n'
              'alpha "alpha"\nbeta "beta"\ngamma "gamma"\n')
    main.write_text(source)
    cold = build(entry=main)
    edited_source = source.replace('"puts", integer, message))\nto beta',
                                   '"puts", integer, "changed"))\nto beta')
    main.write_text(edited_source)
    edited = build(entry=main)
    assert edited[0]["body_resolution_units_reused"] >= 2, edited[0]
    # Reused typed bodies need not reconstruct their earlier matcher results.
    assert edited[0]["cached_body_resolution_references_skipped"] >= 4, edited[0]
    assert edited[0]["body_resolution_reference_visits"] < cold[0]["body_resolution_reference_visits"], edited[0]
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2].splitlines() == [b"changed", b"beta", b"gamma"]
    main.write_text(edited_source + 'to unrelated: @intrinsic("discard", 0)\n')
    added = build(entry=main)
    assert added[0]["body_resolution_units_reused"] >= 2, added[0]
    assert added[1:] == build(False, entry=main)[1:]

    # A newly available single-word pattern changes the old failed lookup of
    # `local`; its enclosing body must be resolved again and report the error.
    main.write_text(edited_source + 'local means: "replacement"\n')
    invalid = build(entry=main, success=False)
    assert invalid[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(edited_source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # A new definition sharing a common start is irrelevant if another
    # required literal cannot occur in any of the unchanged bodies.
    main = root / "body_unit_shared_pattern_start.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to report {message}: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'to first: report "first"\n'
              'to second: report "second"\n'
              'first\nsecond\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source + 'to report audit {message}: @intrinsic("discard", message)\n')
    added = build(entry=main)
    assert added[0]["body_resolution_units_reused"] >= 3, added[0]["body_resolution_units_reused"]
    assert added[1:] == build(False, entry=main)[1:]
    assert added[2].splitlines() == [b"first", b"second"]

    # Removing the last parameter use changes a declaration and its callers.
    main = root / "body_unit_classification.dl"
    source = ('to consume value: @intrinsic("discard", value)\n'
              'to caller {item}: consume item\n'
              'caller 1\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", value', '"discard", 0'))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # Parameter classification still runs first; its independent deferred
    # references can reuse the body-resolution unit once that signature settles.
    main = root / "mixed_body_units.dl"
    source = ('to combined value:\n'
              '    @intrinsic("discard", value)\n'
              '    @intrinsic("store", local, 1)\n'
              '    @intrinsic("discard", local)\n'
              'to unused: @intrinsic("discard", 0)\n'
              'combined 1\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 0', '"discard", 2'))
    restored = build(entry=main)
    assert restored[0]["body_resolution_units_reused"] >= 1, restored[0]
    assert restored[0]["cached_body_resolution_references_skipped"] >= 2, restored[0]
    assert restored[1:] == build(False, entry=main)[1:]

    # Explicit names can span several words and contain a parsed number.
    # Replaying their variable result must retain the original source range.
    for index, name in enumerate(("signed octet", "item 5")):
        main = root / f"body_unit_variable_span_{index}.dl"
        source = ('to get echo {' + name + '}:\n    @intrinsic("return", ' + name + ')\n'
                  'to unused: @intrinsic("discard", 0)\n'
                  '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", echo 7)\n')
        main.write_text(source)
        build(entry=main)
        main.write_text(source.replace('"discard", 0', '"discard", 2'))
        restored = build(entry=main)
        assert restored[0]["body_resolution_units_reused"] >= 1, restored[0]
        assert restored[0]["cached_body_resolution_references_skipped"] >= 1, restored[0]
        assert restored[1:] == build(False, entry=main)[1:]
        assert restored[2] == b"7\n"
