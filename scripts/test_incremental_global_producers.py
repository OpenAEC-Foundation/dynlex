"""Global type products validate the execution prefix that produced them."""

from test_incremental_program_cache import read_program

def check_global_type_producers(root, build):
    main = root / 'global_type_producers.dl'
    source = ('@intrinsic("store", shared, @intrinsic("command line argument count"))\n'
              'function show:\n'
              '    globals: shared\n'
              '    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", @intrinsic("add", shared, 1))\n'
              'show\n')
    main.write_text(source)
    build(entry=main)
    graph = read_program(root / 'cache', main)['section_dependency_graph']
    assert len(graph['global_type_producers']) == 1
    for value in (2, 3):
        main.write_text(source.replace('shared, 1)', f'shared, {value})'))
        changed = build(entry=main)
        assert changed[1:] == build(False, entry=main)[1:]
        assert changed[2] == f'{value}\n'.encode()
        assert changed[0]['root_execution_reused']
        assert changed[0]['global_type_producers_restored'] == 1
        assert changed[0]['preinferred_instantiations'] == 1

    # A newly introduced read cannot assume a global exists at an earlier call.
    early = ('function show:\n'
             '    globals: shared\n'
             '    execute:\n'
             '        @intrinsic("discard", 1)\n'
             'show\n'
             '@intrinsic("store", shared, @intrinsic("command line argument count"))\n')
    main.write_text(early)
    build(entry=main)
    main.write_text(early.replace('"discard", 1', '"discard", shared'))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]

    # Changing a type provider invalidates its output and the global reader.
    provider = ('to get initial: @intrinsic("return", 1)\n'
                '@intrinsic("store", shared, initial)\n'
                'function show:\n'
                '    globals: shared\n'
                '    execute:\n'
                '        @intrinsic("variadic call", "libc", "printf", '
                '@intrinsic("type", "nothing"), 1, "%d\\n", '
                '@intrinsic("cast", shared, @intrinsic("type", "int")))\n'
                'show\n')
    main.write_text(provider)
    build(entry=main)
    main.write_text(provider.replace('"return", 1', '"return", 2'))
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(provider.replace('"return", 1', '"return", 2.5'))
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # The same unchanged producer proof can feed another producer in the prefix.
    chained = source.replace(
        'function show:', '@intrinsic("store", second, @intrinsic("add", shared, 5))\nfunction show:'
    ).replace('globals: shared', 'globals: second').replace('"add", shared, 1', '"add", second, 1')
    main.write_text(chained)
    build(entry=main)
    main.write_text(chained.replace('second, 1)', 'second, 2)'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]['root_execution_reused']
    assert changed[0]['global_type_producers_restored'] == 2

    # Globals initialized inside a call must not validate that call's own edit.
    setup = ('function setup:\n'
             '    globals: shared\n'
             '    execute:\n'
             '        @intrinsic("store", shared, @intrinsic("command line argument count"))\n'
             '        @intrinsic("discard", shared)\n'
             'setup\n')
    main.write_text(setup)
    build(entry=main)
    reordered = setup.replace(
        '        @intrinsic("store", shared, @intrinsic("command line argument count"))\n'
        '        @intrinsic("discard", shared)\n',
        '        @intrinsic("discard", shared)\n'
        '        @intrinsic("store", shared, @intrinsic("command line argument count"))\n'
    )
    main.write_text(reordered)
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]

    # Restoring a class output requires its current structural field identity.
    structured = ('class:\n'
                  '    patterns: record\n'
                  '    members: number\n'
                  '@intrinsic("store", shared, @intrinsic("construct", record, '
                  '@intrinsic("command line argument count")))\n' +
                  source[source.index('function show:'):].replace('"add", shared, 1', '"add", shared\'s number, 1'))
    main.write_text(structured)
    build(entry=main)
    main.write_text(structured.replace("shared's number, 1)", "shared's number, 2)"))
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
