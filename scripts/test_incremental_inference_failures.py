"""Concrete semantic results survive errors independently of native output."""


def check_failed_units(root, build):
    folder = root / 'inference_failure_units'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('to healthy {value}: @intrinsic("discard", @intrinsic("add", value, 0))\n'
              'to broken {value}: @intrinsic("discard", missing)\n'
              'healthy 1\nbroken 1\n')
    main.write_text(source)
    first = build(entry=main, success=False)
    assert first[1:] == build(False, entry=main, success=False)[1:]
    assert first[0]['function_body_inference_passes'] == 2
    changed = source.replace('"add", value, 0', '"add", value, 1')
    main.write_text(changed)
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    assert edited[0]['function_body_inference_passes'] == 1
    assert edited[0]['inference_failures_reused'] == 1
    main.write_text(changed + '@intrinsic(\n')
    build(entry=main, success=False)
    main.write_text(changed)
    recovered = build(entry=main, success=False)
    assert recovered[1:] == build(False, entry=main, success=False)[1:]
    assert recovered[0]['function_body_inference_passes'] == 0
    main.write_text(changed + 'missing means: 0\n')
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]['function_body_inference_passes'] == 1
    assert repaired[0]['inference_instantiations_reused'] > 0

def check_failed_native_bridge(root, build):
    # A failed semantic snapshot can be newer than the last native executable.
    # An interface comparison against that newer snapshot cannot validate old IR.
    folder = root / 'inference_failure_native'
    folder.mkdir()
    native = folder / 'native_bridge.dl'
    initial = ('to get answer:\n    @intrinsic("return", @intrinsic("command line argument count"))\n'
               'to report: @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, '
               '"%lld\\n", @intrinsic("cast", @intrinsic("add", answer, 1), @intrinsic("type", "int", 64)))\n'
               'to broken: @intrinsic("discard", missing)\nreport\n')
    native.write_text(initial)
    assert build(entry=native)[2] == b'1\n'
    changed = initial.replace('@intrinsic("command line argument count"))',
                              '@intrinsic("add", @intrinsic("command line argument count"), 4294967296))')
    native.write_text(changed + 'broken\n')
    failed = build(entry=native, success=False)
    assert failed[1:] == build(False, entry=native, success=False)[1:]
    native.write_text(changed.replace('4294967296', '4294967297'))
    recovered = build(entry=native)
    assert recovered[2] == b'4294967298\n'
    assert recovered[1:] == build(False, entry=native)[1:]

def check_failed_caller_trace(root, build):
    # Failure traces are relative to their concrete unit, not an earlier caller.
    folder = root / 'inference_failure_trace'
    folder.mkdir()
    main = folder / 'caller.dl'
    source = ('to broken {value}:\n'
              '    @intrinsic("discard", @intrinsic("multiply", value, "text"))\n'
              'to wrapper {value}: broken value\n'
              'wrapper 1\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('wrapper 1', '@intrinsic("discard", 3)\nwrapper 2'))
    moved = build(entry=main, success=False)
    assert moved[1:] == build(False, entry=main, success=False)[1:]
    assert moved[0]['function_body_inference_passes'] == 0
    assert moved[0]['inference_failures_reused'] > 0

def check_failed_class_units(root, build):
    # Class layouts and successful typed bodies must also survive failed builds.
    folder = root / 'inference_failure_class'
    folder.mkdir()
    main = folder / 'class.dl'
    source = ('class:\n    patterns:\n        box\n    members:\n        number\n'
              'to get healthy {value}:\n    @intrinsic("return", @intrinsic("construct", box, value))\n'
              'to broken {value}: @intrinsic("discard", @intrinsic("multiply", value, 2))\n'
              'broken (healthy 1)\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('healthy 1)', 'healthy 2)'))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:]
    assert changed[0]['function_body_inference_passes'] == 0
    assert changed[0]['inference_failures_reused'] > 0
    main.write_text(source.replace('@intrinsic("multiply", value, 2)', 'value'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]['function_body_inference_passes'] == 1

def check_failed_wording(root, build):
    # Wording-only configuration changes still replay the complete error result.
    wording_folder = root / 'inference_failure_wording'
    wording_folder.mkdir()
    main = wording_folder / 'main.dl'
    config = wording_folder / 'config.dl'
    source = 'to broken: @intrinsic("discard", missing)\nbroken\n'
    main.write_text(source)
    wording = ('dynlex options:\n    messages:\n'
               '        variable has no type: "First wording for {name}"\n')
    config.write_text(wording)
    first = build(entry=main, success=False)
    assert 'First wording' in first[1]
    config.write_text(wording.replace('First', 'Second'))
    renamed = build(entry=main, success=False)
    assert renamed[1:] == build(False, entry=main, success=False)[1:]
    assert renamed[0]['frontend_reused'] and renamed[0]['function_body_inference_passes'] == 0
    assert renamed[0]['objects_built'] == 0 and renamed[0]['link_ms'] == 0
    main.write_text('@intrinsic("discard", 3)\n' + source)
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    assert 'Second wording' in edited[1]
    assert edited[0]['function_body_inference_passes'] == 0
    main.write_text('to broken {value}: @intrinsic("discard", @intrinsic("multiply", value, "text"))\nbroken 1\n')
    wording = ('dynlex options:\n    messages:\n'
               '        incompatible operand types: "First math wording: {left_type} and {right_type}"\n')
    config.write_text(wording)
    first = build(entry=main, success=False)
    assert 'First math wording' in first[1]
    config.write_text(wording.replace('First', 'Second'))
    renamed = build(entry=main, success=False)
    assert renamed[1:] == build(False, entry=main, success=False)[1:]
    assert 'Second math wording' in renamed[1]
    assert renamed[0]['frontend_reused'] and renamed[0]['function_body_inference_passes'] == 0
    main.write_text('@intrinsic("discard", 4)\n' + main.read_text())
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    assert edited[0]['function_body_inference_passes'] == 0


def check_failed_exposed_unit(root, build):
    folder = root / 'inference_failure_exposed'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('exposed function broken:\n    execute:\n        @intrinsic("discard", missing)\n'
              'to healthy: @intrinsic("discard", 1)\nhealthy\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:]
    assert changed[0]['function_body_inference_passes'] == 1
    assert changed[0]['inference_failures_reused'] > 0


def check_provisional_inference_failure(root, build):
    folder = root / 'inference_failure_provisional'
    folder.mkdir()
    for operation in ('discard', 'store'):
        main = folder / (operation + '.dl')
        arguments = '' if operation == 'discard' else 'answer, '
        source = ('function spare:\n    globals:\n        amount\n    execute:\n'
                  f'        @intrinsic("{operation}", {arguments}@intrinsic("add", amount, 1))\n'
                  'to runner: @intrinsic("discard", 0)\n'
                  '@intrinsic("store", amount, 7)\nrunner\n')
        main.write_text(source)
        build(entry=main)
        main.write_text(source.replace('to runner: @intrinsic("discard", 0)',
                        'to runner: @intrinsic("call pointer", @intrinsic("function", "spare"), '
                        '@intrinsic("type", "nothing"))'))
        changed = build(entry=main)
        assert changed[1:] == build(False, entry=main)[1:]


def check_failed_conversion_lookup(root, build):
    folder = root / 'inference_failure_conversion'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('integer means: @intrinsic("type", "int")\n'
              'class:\n    patterns:\n        box\n    members:\n        number as integer\n'
              'to healthy: @intrinsic("discard", 0)\n'
              'to consume {value}: @intrinsic("discard", @intrinsic("cast", value, box))\n'
              'healthy\nconsume @intrinsic("command line argument count")\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source + 'convert {integer:value}:\n'
                    '    execute: @intrinsic("return", @intrinsic("construct", box, value))\n')
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]['callable_lookups_changed'] > 0
    assert repaired[0]['inference_instantiations_reused'] > 0


def check_failed_conversion_domain(root, build):
    folder = root / 'inference_failure_conversion_domain'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('integer means: @intrinsic("type", "int")\n'
              'domain means: @intrinsic("type", "float")\n'
              'class:\n    patterns:\n        box\n    members:\n        number as integer\n'
              'convert {domain:value}:\n'
              '    execute: @intrinsic("return", @intrinsic("construct", box, value))\n'
              'to healthy: @intrinsic("discard", 0)\n'
              'to consume {value}: @intrinsic("discard", @intrinsic("cast", value, box))\n'
              'healthy\nconsume @intrinsic("command line argument count")\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('domain means: @intrinsic("type", "float")',
                                   'domain means: @intrinsic("type", "int")'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]['inference_instantiations_reused'] > 0
    main.write_text(source)
    rejected = build(entry=main, success=False)
    assert rejected[1:] == build(False, entry=main, success=False)[1:]


def check_trial_product_views(root, build):
    folder = root / 'inference_trial_product_views'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('integer means: @intrinsic("type", "int")\n'
              'class:\n    patterns:\n        box\n    members:\n        number as integer\n'
              'class:\n    patterns:\n        spare\n    members:\n        number as integer\n'
              'to get shifted {value}:\n    @intrinsic("return", @intrinsic("add", value, 1))\n'
              'convert {integer:value}:\n    execute: @intrinsic("return", @intrinsic("construct", box, value))\n'
              'convert {integer:other}:\n    execute: @intrinsic("return", @intrinsic("construct", spare, shifted other))\n'
              'to consume {value}: @intrinsic("discard", @intrinsic("cast", value, box))\n'
              'consume @intrinsic("command line argument count")\n')
    main.write_text(source)
    clean = build(False, entry=main)
    generated = build(entry=main)
    assert generated[1:] == clean[1:]
    assert generated[0]['inference_source_index_builds'] == 1
    assert generated[0]['inference_source_expressions_indexed'] > 0
    unchanged = build(entry=main)
    assert unchanged[0]['frontend_reused'] and unchanged[0]['inference_source_index_builds'] == 0
    main.write_text(source + '@intrinsic("discard", 1)\n')
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]


def check_retained_trial_observations(root, build):
    from test_incremental_program_cache import read_program
    folder = root / 'inference_retained_trial_observations'
    folder.mkdir()
    main = folder / 'main.dl'
    source = ('integer means: @intrinsic("type", "int")\n'
              'to get shifted {value}:\n    @intrinsic("return", @intrinsic("add", value, 1))\n')
    for name in ('box', 'spare a', 'spare b'):
        source += f'class:\n    patterns:\n        {name}\n    members:\n        number as integer\n'
    for index, name in enumerate(('box', 'spare a', 'spare b')):
        source += f'convert {{integer:value{index}}}:\n    execute:\n'
        if index:
            source += f'        @intrinsic("discard", shifted {index + 4})\n'
        source += f'        @intrinsic("return", @intrinsic("construct", {name}, value{index}))\n'
    source += ('@intrinsic("discard", shifted 0)\n'
               '@intrinsic("discard", @intrinsic("cast", @intrinsic("command line argument count"), box))\n')
    main.write_text(source)
    generated = build(entry=main)
    assert generated[1:] == build(False, entry=main)[1:]
    changed = source.replace('shifted 5', 'shifted 8').replace(
        '@intrinsic("discard", shifted 0)\n',
        '@intrinsic("discard", shifted 0)\n@intrinsic("discard", shifted @intrinsic("command line argument count"))\n')
    for text, count in ((source, 3), (changed, 4), (changed + '@intrinsic("discard", shifted 7)\n', 5)):
        main.write_text(text)
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        program = read_program(root / 'cache', main)
        summaries = [summary for variants in program['inference_summaries'].values() for summary in variants.values()]
        assert any(len(summary.get('pure_values', [])) == count for summary in summaries), \
            'rollback or the final snapshot dropped a pure argument observation'
        if count == 5:
            assert edited[0]['function_body_inference_passes'] == 0, 'observation refresh lost the saved typed body'


def check_inference_failures(root, build):
    check_failed_units(root, build)
    check_failed_native_bridge(root, build)
    check_failed_caller_trace(root, build)
    check_failed_class_units(root, build)
    check_failed_wording(root, build)
    check_failed_exposed_unit(root, build)
    check_provisional_inference_failure(root, build)
    check_failed_conversion_lookup(root, build)
    check_failed_conversion_domain(root, build)
    check_trial_product_views(root, build)
    check_retained_trial_observations(root, build)
