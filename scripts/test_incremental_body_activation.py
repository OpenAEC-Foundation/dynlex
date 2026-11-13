"""A changed caller can activate an unchanged body through a function value."""


def check_inactive_body_activation(root, build):
    main = root / "inactive_callback_activation.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to spare:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "spare"))\n'
        'to runner:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        'runner\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"before\n"
    changed = source.replace(
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))',
        '    @intrinsic("call pointer", @intrinsic("function", "spare"), @intrinsic("type", "nothing"))',
    )
    main.write_text(changed)
    activated = build(entry=main)
    assert activated[1:] == build(False, entry=main)[1:], "new function value consumed an unresolved cached body"
    assert activated[2] == b"spare\n"
    assert activated[0]["cached_body_resolution_references_skipped"] > 0
    assert activated[0]["body_resolution_references_restored"] > 0
    main.write_text(changed.replace('integer, "spare"', 'integer, "updated spare"'))
    updated = build(entry=main)
    assert updated[1:] == build(False, entry=main)[1:]
    assert updated[2] == b"updated spare\n", "a newly retained callback lost its invalidation dependencies"
    for text in (source, changed):
        main.write_text(text)
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == (b"spare\n" if text == changed else b"before\n")

    # The cached body carries lexical variable bindings before inference,
    # including a parameter, a local, and a declared root global.
    main = root / "inactive_callback_bindings.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function spare {integer:seed}:\n'
        '    globals:\n'
        '        amount\n'
        '    execute:\n'
        '        @intrinsic("store", answer, @intrinsic("add", seed, amount))\n'
        '        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", answer))\n'
        'to runner: @intrinsic("discard", 0)\n'
        '@intrinsic("store", amount, 7)\n'
        'runner\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b""
    main.write_text(source.replace(
        'to runner: @intrinsic("discard", 0)',
        'to runner: @intrinsic("call pointer", @intrinsic("function", "spare $"), @intrinsic("type", "nothing"), 5)',
    ))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"12\n"
    assert edited[0]["cached_body_resolution_references_skipped"] > 0
    assert edited[0]["body_resolution_references_restored"] > 0
