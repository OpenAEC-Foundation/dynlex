"""Deferred source declarations participate in constraint queries before inference."""

from test_incremental_program_cache import read_program


def check_declared_constraint_sources(root, build):
    cases = (
        ('local', 'to get chosen {value}:\n'
         '    @intrinsic("store", {integer:hidden}, value)\n'
         '    @intrinsic("return", hidden)\n'),
        ('parameter', 'to get chosen {value}:\n'
         '    @intrinsic("return", @intrinsic("add", {integer:value}, value))\n'),
    )
    for name, definition in cases:
        main = root / f'deferred_{name}_constraint.dl'
        source = ('integer means: @intrinsic("type", "int")\n' + definition +
                  '@intrinsic("discard", chosen 1)\n')
        main.write_text(source)
        build(entry=main)
        main.write_text(source.replace('chosen 1)', 'chosen 2.5)'))
        assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
        main.write_text(source.replace('chosen 1)', 'chosen 2)'))
        repaired = build(entry=main)
        assert repaired[1:] == build(False, entry=main)[1:]
        assert repaired[0]['binding_constraint_demands'] >= 1
        assert repaired[0]['constraint_source_units_reused'] >= 1
        assert repaired[0]['constraint_source_references_scanned'] == 0
        projection = read_program(root / 'cache', main)['all_function_projection']['matcher_projection']
        assert len(projection['constraint_sources']) == 1

        # A changed provider changes the constraint even when the body is unchanged.
        floating = source.replace('"int"', '"float"').replace('chosen 1)', 'chosen 2.5)')
        main.write_text(floating)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        main.write_text(floating.replace('integer means:', 'other means:'))
        assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
        main.write_text(source)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

        # Removing the annotation removes its query and permits the float input.
        untyped = source.replace('{integer:hidden}', 'hidden').replace('{integer:value}', 'value')
        main.write_text(untyped.replace('chosen 1)', 'chosen 2.5)'))
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        projection = read_program(root / 'cache', main)['all_function_projection']['matcher_projection']
        assert not projection['constraint_sources']
        main.write_text(source)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # Declared constraints are checked in bodies without a runtime instantiation.
    main = root / 'deferred_unused_constraint.dl'
    source = ('integer means: @intrinsic("type", "int")\n' + cases[0][1] +
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    unused = build(entry=main)
    assert unused[1:] == build(False, entry=main)[1:]
    assert unused[0]['binding_constraint_demands'] >= 1
    main.write_text(source.replace('integer means:', 'other means:'))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
