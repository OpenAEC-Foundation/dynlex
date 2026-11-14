"""Whole cached body results survive non-use and become usable on later demand."""


def check_body_resolution_demand(root, build):
    check_negative_body_dependency(root, build)
    check_reactivated_local_variables(root, build)
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


def check_negative_body_dependency(root, build):
    main = root / "body_negative_dependency.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to spare {integer:seed}: @intrinsic("discard", seed)\n'
              'to runner: @intrinsic("discard", 0)\nrunner\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    deferred = build(entry=main)
    assert deferred[0]["body_resolution_units_deferred"] >= 1
    main.write_text(source.replace('to runner: @intrinsic("discard", 0)',
        'to runner: @intrinsic("call pointer", @intrinsic("function", "spare $"), '
        '@intrinsic("type", "nothing"), 5)'))
    assert build(entry=main)[1:] == build(False, entry=main)[1:], \
        "a deferred variable reference lost its negative lookup dependency"


def check_reactivated_local_variables(root, build):
    main = root / "body_reactivated_local_variables.dl"
    source = (
        'to get inspect collection values:\n'
        '    @intrinsic("store", item, @intrinsic("extract element", values, 0))\n'
        '    @intrinsic("return", item)\n'
        '@intrinsic("store", first, 1)\n'
        '@intrinsic("discard", first)\n'
    )
    main.write_text(source)
    assert build(entry=main)[1:] == ("", b"")
    for value in (1, 2):
        active = source.replace('first, 1', f'first, {value}').replace(
            '@intrinsic("discard", first)',
            '@intrinsic("store", {first group}, [first])\n'
            '@intrinsic("return", @intrinsic("subtract", '
            f'inspect collection {{first group}}, {value}))')
        main.write_text(active)
        clean = build(False, entry=main)
        assert clean[1:] == ("", b"")
        assert build(entry=main)[1:] == clean[1:], \
            "reopened definitions left their local variables blocked"
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:] == ("", b"")
