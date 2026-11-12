"""Speculative inference must publish diagnostics in the real calling context."""

import re


def check_persistent_diagnostic_units(root, build):
    folder = root / 'persistent_diagnostic_units'
    folder.mkdir()
    main = folder / 'main.dl'
    source = '''integer means: @intrinsic("type", "int")
function return value:
    replacement: @intrinsic("return", value)
to get left - right:
    return @intrinsic("subtract", left, right)
to get value adjusted:
    return value
to get the tested form of value:
    return value - 1 adjusted
to get the wrapped form of value:
    return the tested form of value
to stable:
    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "stable"))
to edited:
    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))
'''
    direct = '@intrinsic("discard", the tested form of @intrinsic("command line argument count"))\n'
    wrapped = '@intrinsic("discard", the wrapped form of @intrinsic("command line argument count"))\n'
    initial = source + wrapped + 'stable\nedited\n'
    main.write_text(initial)
    cold = build(entry=main)
    assert cold[1].count('Warning:') == 1
    changed = initial.replace('integer, "before"', 'integer, "after"')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]['function_body_inference_passes'] < cold[0]['function_body_inference_passes'], \
        'one warning excluded every inference unit in its source file'
    assert edited[0]['root_execution_reused'], 'a retained warning forced entry-point inference'
    assert edited[0]['functions_ir_reused'] >= 4
    assert edited[0]['inference_diagnostic_units_restored'] >= 1
    assert edited[0]['inference_diagnostic_records_restored'] >= 1

    # Moving a warning's callers changes its trace, while its body remains cached.
    for calls in (direct + wrapped, wrapped + direct, direct, wrapped):
        main.write_text(changed[:changed.index(wrapped)] + calls + 'stable\nedited\n')
        moved = build(entry=main)
        assert moved[1:] == build(False, entry=main)[1:], 'a cached warning retained its old caller trace'
        assert moved[1].count('Warning:') == 1
        assert moved[0]['inference_instantiations_reused'] >= 2

    # A previously warning-producing child can become warning-free without
    # changing its return type. Its cached parents must drop the old warning.
    clean_source = source.replace('return value - 1 adjusted', 'return (value - 1) adjusted')
    main.write_text(clean_source + wrapped + 'stable\nedited\n')
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert not removed[1], 'an equivalent return type retained a stale diagnostic'
    main.write_text(source + wrapped + 'stable\nedited\n')
    added = build(entry=main)
    assert added[1:] == build(False, entry=main)[1:]
    assert added[1].count('Warning:') == 1
    main.write_text(source + 'stable\nedited\n')
    unused = build(entry=main)
    assert unused[1:] == build(False, entry=main)[1:]
    assert not unused[1], 'an unused cached diagnostic was published'

    # Retained warnings use the current configured wording after a code edit.
    config = folder / 'config.dl'
    config.write_text('dynlex options:\n    messages:\n'
                      '        ambiguous operand grouping: "New wording for {expression}"\n')
    main.write_text(source + wrapped + 'stable\nedited\n')
    build(entry=main)
    main.write_text(initial.replace('integer, "before"', 'integer, "after"'))
    configured = build(entry=main)
    assert configured[1:] == build(False, entry=main)[1:]
    assert 'New wording' in configured[1]
    assert configured[0]['root_execution_reused']

    # Root observations exclude later exposed-function validation. Replaying an
    # entry warning must preserve the order and multiplicity of those warnings.
    exposed = ('to get the other form of value:\n    return value - 1 adjusted\n'
               'exposed function another:\n'
               '    execute: @intrinsic("discard", the other form of @intrinsic("command line argument count"))\n')
    main.write_text(source + exposed + wrapped + 'stable\nedited\n')
    build(entry=main)
    main.write_text(source.replace('integer, "before"', 'integer, "after"') + exposed + wrapped + 'stable\nedited\n')
    phase_replay = build(entry=main)
    assert phase_replay[0]['root_execution_reused']
    assert phase_replay[1:] == build(False, entry=main)[1:]
    assert phase_replay[1].count('Warning:') == 2
    revised_exposed = exposed.replace('return value - 1 adjusted', 'return (value - 1) adjusted')
    main.write_text(source.replace('integer, "before"', 'integer, "after"') + revised_exposed + wrapped + 'stable\nedited\n')
    phase_removed = build(entry=main)
    assert phase_removed[0]['root_execution_reused']
    assert phase_removed[1:] == build(False, entry=main)[1:]
    assert phase_removed[1].count('Warning:') == 1, 'entry replay published a stale exposed-stage warning'

    # Each typed unit needs its own product even when another unit's warning
    # suppressed its publication in the preceding build.
    byte = ('@intrinsic("discard", the tested form of @intrinsic("cast", '
            '@intrinsic("command line argument count"), @intrinsic("type", "int", 8)))\n')
    main.write_text(source + direct + byte + 'stable\nedited\n')
    variants = build(entry=main)
    assert variants[1].count('Warning:') == 1
    main.write_text(source + byte + direct + 'stable\nedited\n')
    reordered = build(entry=main)
    assert reordered[1:] == build(False, entry=main)[1:], 'a suppressed typed warning was absent from its cached unit'
    assert reordered[1].count('Warning:') == 1


def check_preinferred_diagnostic_context(root, build, output):
    source = '''integer means: @intrinsic("type", "int")
function return value:
    replacement: @intrinsic("return", value)
to get left - right:
    return @intrinsic("subtract", left, right)
to get value adjusted:
    return value
to get the tested form of value:
    return (value - 1) adjusted
to get the wrapped form of value:
    return the tested form of value
to stable:
    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "stable"))
'''
    direct = '@intrinsic("discard", the tested form of @intrinsic("command line argument count"))\n'
    wrapped = '@intrinsic("discard", the wrapped form of @intrinsic("command line argument count"))\n'
    for index, calls in enumerate((direct, wrapped, direct + wrapped, wrapped + direct)):
        main = root / f"preinferred_diagnostics_{index}.dl"
        initial = source + calls + 'stable\n'
        main.write_text(initial)
        assert not build(entry=main)[1]
        changed = initial.replace('(value - 1) adjusted', 'value - 1 adjusted')
        main.write_text(changed)
        warned = build(entry=main)
        assert warned[0]["preinferred_instantiations"] > 0
        assert warned[0]["preinferred_diagnostics_deferred"] > 0
        assert warned[0]["deferred_inference_diagnostics_published"] == 1
        assert warned[1:] == build(False, entry=main)[1:], "preinference changed warning caller notes or ordering"
        assert warned[1].count('Warning:') == 1, "a shared diagnostic was published more than once"
        main.write_text(initial)
        recovered = build(entry=main)
        assert recovered[1:] == build(False, entry=main)[1:]
        assert not recovered[1]

    main = root / "preinferred_diagnostic_removed_call.dl"
    initial = source + direct + 'stable\n'
    main.write_text(initial)
    build(entry=main)
    main.write_text(source.replace('(value - 1) adjusted', 'value - 1 adjusted') + 'stable\n')
    removed = build(entry=main)
    assert removed[0]["deferred_inference_diagnostics_published"] == 0
    assert removed[1:] == build(False, entry=main)[1:], "an unused speculative body published a diagnostic"
    assert not removed[1]

    main = root / "preinferred_diagnostic_new_variant.dl"
    initial = source + direct + 'stable\n'
    main.write_text(initial)
    build(entry=main)
    changed = (initial.replace('(value - 1) adjusted', 'value - 1 adjusted')
               .replace('the tested form of @intrinsic("command line argument count")',
                        'the tested form of @intrinsic("cast", @intrinsic("command line argument count"), '
                        '@intrinsic("type", "int", 8))'))
    main.write_text(changed)
    variant = build(entry=main)
    assert variant[1:] == build(False, entry=main)[1:], "an unused old variant suppressed the new variant's warning"
    assert variant[1].count('Warning:') == 1

    main = root / "preinferred_diagnostic_new_child.dl"
    initial = source.replace('return the tested form of value', 'return value') + wrapped + 'stable\n'
    main.write_text(initial)
    build(entry=main)
    changed = (initial.replace('return (value - 1) adjusted', 'return value - 1 adjusted')
               .replace('to get the wrapped form of value:\n    return value',
                        'to get the wrapped form of value:\n    return the tested form of value'))
    main.write_text(changed)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    for index, calls in enumerate(("other\ntested\n", "tested\nother\n")):
        folder = root / f"preinferred_diagnostic_order_{index}"
        folder.mkdir()
        helper = folder / "helper.dl"
        main = folder / "main.dl"
        helper.write_text(source)
        main.write_text('import helper.dl\n'
                        'to get the other form of value:\n    return value - 1 adjusted\n'
                        'to other: @intrinsic("discard", the other form of @intrinsic("command line argument count"))\n'
                        'to tested: @intrinsic("discard", the tested form of @intrinsic("command line argument count"))\n'
                        + calls)
        before = build(entry=main)
        assert before[1].count('Warning:') == 1
        helper.write_text(source.replace('(value - 1) adjusted', 'value - 1 adjusted'))
        edited = build(entry=main)
        assert edited[0]["preinferred_instantiations"] > 0
        assert edited[1:] == build(False, entry=main)[1:], "speculation reordered diagnostics from distinct units"
        assert edited[1].count('Warning:') == 2

    for index, suffix in enumerate((
        'to get answer:\n    @intrinsic("return", the tested form of @intrinsic("command line argument count"))\n'
        '@intrinsic("store", callback, @intrinsic("function", "answer"))\n'
        '@intrinsic("discard", @intrinsic("call pointer", callback, integer))\n',
        'exposed function answer:\n'
        '    execute: @intrinsic("return", the tested form of @intrinsic("command line argument count"))\n',
    )):
        main = root / f"preinferred_diagnostic_callable_{index}.dl"
        initial = source + suffix + 'stable\n'
        main.write_text(initial)
        build(entry=main)
        main.write_text(initial.replace('(value - 1) adjusted', 'value - 1 adjusted'))
        called = build(entry=main)
        assert called[1:] == build(False, entry=main)[1:], "callable validation lost its diagnostic context"
        assert called[1].count('Warning:') == 1, "callable validation discarded a selected body's warning"

    folder = root / "inference_warning_wording"
    folder.mkdir()
    main = folder / "main.dl"
    config = folder / "config.dl"
    main.write_text(source.replace('(value - 1) adjusted', 'value - 1 adjusted') + direct + 'stable\n')
    wording = ('dynlex options:\n    messages:\n'
               '        ambiguous operand grouping: "First wording for {expression}"\n')
    config.write_text(wording)
    first = build(entry=main)
    assert 'First wording' in first[1]
    output_time = output.stat().st_mtime_ns
    config.write_text(wording.replace('First', 'Second'))
    renamed = build(entry=main)
    assert renamed[0]["frontend_reused"]
    assert renamed[0]["function_body_inference_passes"] == 0
    assert renamed[0]["objects_built"] == 0 and renamed[0]["link_ms"] == 0
    assert output.stat().st_mtime_ns == output_time
    assert 'Second wording' in renamed[1]
    assert renamed[1:] == build(False, entry=main)[1:]

    main = root / "failed_preinferred_exposed.dl"
    valid = '''flex section if condition:
    replacement: @intrinsic("if", condition)
exposed function answer:
    execute:
        if @intrinsic("greater than", @intrinsic("command line argument count"), 0):
            @intrinsic("return", 1)
        @intrinsic("return", 2)
@intrinsic("discard", 0)
'''
    main.write_text(valid)
    build(entry=main)
    main.write_text(valid.replace('"return", 2', '"return", "text"'))
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:], "failed speculation lost the real type error"
    assert 'but this return statement produces' in failed[1]
    repeated = build(entry=main, success=False)
    assert repeated[0]["frontend_reused"] and repeated[1:] == failed[1:]
    main.write_text(valid)
    recovered = build(entry=main)
    assert recovered[1:] == build(False, entry=main)[1:]

    main = root / "recursive_callable_context.dl"
    recursive = '''integer means: @intrinsic("type", "int")
flex section if condition:
    replacement: @intrinsic("if", condition)
to get recursive {integer:value}:
    if @intrinsic("greater than", value, 0):
        @intrinsic("return", recursive @intrinsic("subtract", value, 1))
    @intrinsic("return", recursive value)
the function named name means: @intrinsic("function", name)
@intrinsic("discard", the function named "recursive $")
'''
    main.write_text(recursive)
    error = build(entry=main, success=False)
    assert error[1:] == build(False, entry=main, success=False)[1:]
    assert error[1].startswith(f'{main.name}:'), "a caller trace replaced the failing unit's source file"
    location = re.match(r'.*?\.dl:(\d+):\d+-\d+: Error:', error[1])
    assert location
    declaration = main.read_text().splitlines()[int(location[1]) - 1]
    assert declaration.startswith('to get recursive'), "a caller trace replaced the failing unit's declaration location"
