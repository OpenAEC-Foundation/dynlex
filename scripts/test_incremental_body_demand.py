"""Whole cached body results survive non-use and become usable on later demand."""


def check_body_resolution_demand(root, build):
    main = root / "body_resolution_demand.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to report {text}: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, text))\n'
        'to spare {integer:seed}:\n'
        '    @intrinsic("store", {answer}, @intrinsic("add", seed, 1))\n'
        '    report "spare"\n'
        '    @intrinsic("discard", answer)\n'
        'to idle: report "idle"\n'
        'to runner: report "before"\n'
        'runner\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"before\n"
    changed = source.replace('report "before"', 'report "after"')
    main.write_text(changed)
    untouched = build(entry=main)
    assert untouched[1:] == build(False, entry=main)[1:]
    assert untouched[2] == b"after\n"
    assert untouched[0]["body_resolution_units_deferred"] >= 2, \
        "unused cached bodies restored their detailed matches and bindings"

    # The same unit is kept through another cross-process edit before activation.
    changed = changed.replace('report "after"', 'report "again"')
    main.write_text(changed)
    retained = build(entry=main)
    assert retained[1:] == build(False, entry=main)[1:]
    assert retained[0]["body_resolution_units_deferred"] >= 2
    activated = changed.replace('to runner: report "again"',
        'to runner: @intrinsic("call pointer", @intrinsic("function", "spare $"), '
        '@intrinsic("type", "nothing"), 5)')
    main.write_text(activated)
    used = build(entry=main)
    assert used[1:] == build(False, entry=main)[1:]
    assert used[2] == b"spare\n"
    assert used[0]["body_resolution_units_demanded"] >= 1

    # An unused body's failed lookup must still be checked when its provider goes.
    main.write_text(activated.replace('to report {text}:', 'to renamed {text}:'))
    missing = build(entry=main, success=False)
    assert missing[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(activated)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
