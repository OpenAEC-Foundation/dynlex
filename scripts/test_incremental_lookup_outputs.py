"""Inference consumes resolved lookup results rather than matcher input versions."""

from test_incremental_program_cache import read_program


def check_source_lookup_results(root, build):
    main = root / 'source_lookup_results.dl'
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'class:\n'
              '    patterns: a record\n'
              '    members: number as integer\n'
              'function a record using value:\n'
              '    replacement: @intrinsic("construct", a record, value)\n'
              'function observe:\n'
              '    globals: current\n'
              '    execute: @intrinsic("discard", @intrinsic("property", current, "number"))\n'
              '@intrinsic("store", current, a record using 1)\n'
              'observe\n'
              '@intrinsic("return", @intrinsic("subtract", @intrinsic("property", current, "number"), 1))\n')

    def source_inputs():
        graph = read_program(root / 'cache', main)['section_dependency_graph']
        return {path: value
                for units in graph['inference_units'].values()
                for unit in units.values()
                for path, value in unit['sources'].items() if path != 'root'}

    main.write_text(source)
    initial = build(entry=main)
    assert initial[1:] == build(False, entry=main)[1:]
    before = source_inputs()

    # This changes the candidate tree for "a record", but requires an argument
    # and cannot change that zero-argument class lookup's selected definition.
    main.write_text(source + 'function a record unused:\n'
                    '    replacement: @intrinsic("discard", unused)\n')
    added = build(entry=main)
    assert added[1:] == build(False, entry=main)[1:]
    after = source_inputs()
    common = before.keys() & after.keys()
    assert common, 'the constructor produced no reusable inference source inputs'
    assert all(before[path] == after[path] for path in common), \
        'unchanged lookup results changed an existing inference source input'

    main.write_text(source)
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    restored = source_inputs()
    assert all(restored[path] == before[path] for path in before.keys() & restored.keys())
