"""Retained ordinary owners avoid outline allocation until they are demanded."""


def check_source_owner_outline_reuse(root, build):
    main = root / "source_owner_outline_reuse.dl"
    source = ('to get spare seed:\n'
              '    @intrinsic("return", seed)\n'
              'to runner: @intrinsic("discard", 0)\nrunner\n')
    main.write_text(source)
    assert build(entry=main)[1:] == ("", b"")
    changed = source.replace('"discard", 0', '"discard", 1')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["body_resolution_units_deferred"] >= 1
    assert edited[0]["source_owner_units_retained"] >= 1
    assert edited[0]["source_owner_references_retained"] >= 1
    assert edited[0]["source_outline_nodes_restored"] == 0, \
        "an unchanged unused owner reconstructed its reference outline"
    assert edited[0]["source_outline_pattern_references_created"] == 0
    assert edited[0]["source_outline_variables_created"] == 0
    active = changed.replace('runner\n',
        'runner\n@intrinsic("return", @intrinsic("subtract", spare 7, 7))\n')
    main.write_text(active)
    used = build(entry=main)
    assert used[1:] == build(False, entry=main)[1:]
    assert used[0]["source_outline_nodes_restored"] > 0
    assert used[0]["body_resolution_units_demanded"] >= 1
    relocated = active.replace('    @intrinsic("return", seed)',
        '    @intrinsic("discard", 0)\n    @intrinsic("return", seed)')
    main.write_text(relocated)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text('# comment before declarations\n' + relocated)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    without_parameter = active.replace('"return", seed', '"return", 0')
    main.write_text(without_parameter)
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
