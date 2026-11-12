"""Binding ownership is a persistent product of named lexical queries."""


def check_variable_binding_units(root, build):
    main = root / 'binding_units.dl'
    source = ('to get alpha {seed}:\n'
              '    @intrinsic("store", item, @intrinsic("add", seed, 0))\n'
              '    @intrinsic("return", item)\n'
              'to get beta {seed}:\n'
              '    @intrinsic("store", item, @intrinsic("add", seed, 2))\n'
              '    @intrinsic("return", item)\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d %d\\n", alpha 4, beta 5)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b'4 7\n'
    assert cold[0]['binding_units_evaluated'] >= 2
    changed = source.replace('"add", seed, 0', '"add", seed, 1')
    main.write_text(changed)
    value = build(entry=main)
    assert value[1:] == build(False, entry=main)[1:]
    assert value[2] == b'5 7\n'
    assert value[0]['binding_units_reused'] >= 2
    assert value[0]['binding_inputs_reused'] >= 1
    assert value[0]['binding_units_evaluated'] == 0
    assert value[0]['binding_references_classified'] == 0

    # The same local name in a separate function is an independent query.
    extra = changed.replace('to get beta', '    @intrinsic("discard", item)\nto get beta')
    main.write_text(extra)
    reference = build(entry=main)
    assert reference[1:] == build(False, entry=main)[1:]
    assert reference[0]['binding_units_evaluated'] == 1
    assert reference[0]['binding_units_reused'] >= 1

    # Completed binding products survive a failure in an earlier stage.
    main.write_text(extra + '@intrinsic(\n')
    build(entry=main, success=False)
    main.write_text(extra.replace('"add", seed, 1', '"add", seed, 3'))
    recovered = build(entry=main)
    assert recovered[1:] == build(False, entry=main)[1:]
    assert recovered[2] == b'7 7\n'
    assert recovered[0]['binding_units_evaluated'] == 0
    assert recovered[0]['binding_units_reused'] >= 2
    check_variable_binding_environments(root, build)


def check_variable_binding_environments(root, build):
    main = root / 'binding_globals.dl'
    source = ('@intrinsic("store", item, 1)\n'
              'function update:\n    globals:\n        item\n'
              '    execute: @intrinsic("store", item, @intrinsic("add", item, 1))\n'
              'to get isolated:\n    @intrinsic("store", item, 10)\n    @intrinsic("return", item)\n'
              'update\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d %d\\n", item, isolated)\n')
    main.write_text(source)
    assert build(entry=main)[2] == b'2 10\n'
    changed = source.replace('item, 10', 'item, 20')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b'2 20\n'
    assert edited[0]['binding_units_evaluated'] == 0
    # Removing global visibility changes the global query and update's local
    # query, while the independent function's same-named local stays reusable.
    main.write_text(changed.replace('        item\n', '        other\n'))
    invalid = build(entry=main, success=False)
    assert invalid[1:] == build(False, entry=main, success=False)[1:]
    assert invalid[0]['binding_units_reused'] >= 1
    main.write_text(changed)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # A nested global reader can reach an ancestor's implicit binding. The
    # binding domains must join before deciding whether that is a capture.
    main = root / 'binding_nested_global.dl'
    source = ('to enclosing:\n    @intrinsic("store", item, 3)\n'
              '    function inner:\n        globals:\n            item\n'
              '        execute: @intrinsic("discard", item)\n    inner\nenclosing\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert 'cannot capture outer local variable' in failed[1]
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text('@intrinsic("store", item, 9)\n' + source)
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    main.write_text(source)
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
