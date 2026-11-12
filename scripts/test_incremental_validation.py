"""Typed usage diagnostics and lexical capture checks retain their own products."""


def check_diagnostic_stage_reuse(root, build):
    main = root / 'diagnostic_stage_reuse.dl'
    source = ('to warn {item}: @intrinsic("store", item, 0)\n'
              'to stable {item}: @intrinsic("discard", item)\n'
              'to changing: @intrinsic("discard", 0)\n'
              'warn @intrinsic("command line argument count")\nstable 1\nchanging\n')
    main.write_text(source)
    first = build(entry=main)
    assert 'assigned before being read' in first[1]
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]['classification_units_reused'] >= 2, edited[0]
    assert edited[0]['body_resolution_units_reused'] >= 2, edited[0]
    assert edited[0]['body_resolution_reference_visits'] < first[0]['body_resolution_reference_visits']

    # Resolver warnings must remain visible after an unrelated edit too.
    main = root / 'resolution_warning_stage.dl'
    source = ('element means: 1\n'
              'to choose element: @intrinsic("discard", element)\n'
              'to changing: @intrinsic("discard", 0)\nchoose element\nchanging\n')
    main.write_text(source)
    assert 'Ambiguous single-word reference' in build(entry=main)[1]
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    resolver = build(entry=main)
    assert resolver[1:] == build(False, entry=main)[1:]
    assert 'Ambiguous single-word reference' in resolver[1]


def check_capture_note_wording(root, build):
    folder = root / "capture_note_wording"
    folder.mkdir()
    main = folder / "main.dl"
    config = folder / "config.dl"
    main.write_text('to use {thing}:\n    to inner: @intrinsic("discard", thing)\n    inner\nuse 1\n')
    wording = ('dynlex options:\n    messages:\n        function captures local:\n'
               '            related definition: "Old capture note"\n')
    config.write_text(wording)
    assert 'Old capture note' in build(entry=main, success=False)[1]
    config.write_text(wording.replace('Old capture note', 'New capture note'))
    changed = build(entry=main, success=False)
    assert changed[0]['frontend_reused']
    assert changed[1:] == build(False, entry=main, success=False)[1:]
    assert 'New capture note' in changed[1] and 'Old capture note' not in changed[1]


def check_validation_units(root, build):
    folder = root / "validation_units"
    folder.mkdir()
    main = folder / "main.dl"
    config = folder / "config.dl"
    config.write_text('dynlex options:\n    messages:\n        variable usage warning:\n'
                      '            pattern argument assigned before read: "Pattern argument \'{name}\' is assigned before being read"\n')
    source = ('function write {destination}:\n    replacement: @intrinsic("store", destination, 0)\n'
              'to warn {item}: write item\n'
              'to stable {item}: @intrinsic("discard", item)\n'
              'warn @intrinsic("command line argument count")\nstable 1\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    assert 'assigned before being read' in cold[1]
    edited = source.replace('"discard", item)', '"discard", @intrinsic("add", item, 0))')
    main.write_text(edited)
    reused = build(entry=main)
    assert reused[1:] == build(False, entry=main)[1:]
    assert reused[0]['inference_diagnostic_units_restored'] > 0
    assert reused[0]['inference_diagnostic_records_restored'] > 0
    assert reused[0]['function_body_inference_passes'] < cold[0]['function_body_inference_passes']

    # The warning depends on the flex body, although the warning's own line is unchanged.
    read = edited.replace('"store", destination, 0', '"discard", destination')
    main.write_text(read)
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert not removed[1]
    main.write_text(edited)
    added = build(entry=main)
    assert added[1:] == build(False, entry=main)[1:]
    assert 'assigned before being read' in added[1]

    # A new literal overload changes the unchanged caller's resolved expression.
    main.write_text(edited + 'function write item:\n    replacement: @intrinsic("discard", 0)\n')
    overload = build(entry=main)
    assert overload[1:] == build(False, entry=main)[1:]
    assert not overload[1]
    main.write_text(edited)
    build(entry=main)
    config.write_text('dynlex options:\n    messages:\n        variable usage warning:\n'
                      '            pattern argument assigned before read: "Updated warning {name}"\n'
                      '            related argument definition: "Updated definition"\n')
    main.write_text('# Moved source\n' + edited.replace('"add", item, 0', '"add", item, 1'))
    configured = build(entry=main)
    assert configured[1:] == build(False, entry=main)[1:]
    assert configured[0]['inference_diagnostic_records_restored'] > 0
    assert 'Updated warning' in configured[1] and 'Updated definition' in configured[1]

    # An unused-body edit may reuse all inference while wording is rerendered.
    separate = root / 'configured_inactive_validation'
    separate.mkdir()
    helper = separate / 'helper.dl'
    helper.write_text(source.split('to stable')[0])
    entry = separate / 'main.dl'
    program = ('import helper.dl\nto unused: @intrinsic("discard", 0)\n'
               'warn @intrinsic("command line argument count")\n')
    entry.write_text(program)
    settings = separate / 'config.dl'
    wording = ('dynlex options:\n    messages:\n        variable usage warning:\n'
               '            pattern argument assigned before read: "Old warning {name}"\n')
    settings.write_text(wording)
    assert 'Old warning' in build(entry=entry)[1]
    settings.write_text(wording.replace('Old warning', 'New warning'))
    entry.write_text(program.replace('"discard", 0', '"discard", 1'))
    refreshed = build(entry=entry)
    assert refreshed[0]['type_inference_reused']
    assert refreshed[1:] == build(False, entry=entry)[1:]
    assert 'New warning' in refreshed[1] and 'Old warning' not in refreshed[1]

    # Reusing an enclosing call must retain an untraced declaration warning
    # produced by a nested callee, even when that callee is never called again.
    nested = root / 'nested_usage_warning.dl'
    program = ('to warn {item}: @intrinsic("store", item, 0)\n'
               'to enclosing: warn @intrinsic("command line argument count")\n'
               'to independent {item}: @intrinsic("discard", item)\n'
               'enclosing\nindependent 1\n')
    nested.write_text(program)
    first = build(entry=nested)
    assert 'assigned before being read' in first[1]
    nested.write_text(program.replace('independent 1', 'independent 2'))
    retained = build(entry=nested)
    assert retained[1:] == build(False, entry=nested)[1:]
    assert retained[0]['inference_diagnostic_records_restored'] > 0
    nested.write_text(program.replace('\nenclosing\n', '\n'))
    unreachable = build(entry=nested)
    assert unreachable[1:] == build(False, entry=nested)[1:]
    assert not unreachable[1]


def check_failed_validation_units(root, build):
    folder = root / "failed_validation_units"
    folder.mkdir()
    main = folder / "main.dl"
    source = ('to independent {value}: @intrinsic("discard", value)\n'
              'to use {thing}:\n    to inner: @intrinsic("discard", thing)\n    inner\n'
              'independent 1\nuse 1\n')
    main.write_text(source)
    cold = build(entry=main, success=False)
    assert cold[1:] == build(False, entry=main, success=False)[1:]
    changed = source.replace('"discard", value)', '"discard", @intrinsic("add", value, 0))')
    main.write_text(changed)
    reused = build(entry=main, success=False)
    assert reused[1:] == build(False, entry=main, success=False)[1:]
    assert reused[0]['validation_units_reused'] > 0
    assert reused[0]['validation_diagnostics_restored'] > 0
    assert reused[0]['validation_capture_references_visited'] < cold[0]['validation_capture_references_visited']
    # Binding identities alone cannot prove that a cached diagnostic's source
    # anchor still exists after editing the line that produced it.
    main.write_text(changed.replace('"discard", thing)', '"discard", @intrinsic("add", thing, 0))'))
    relocated = build(entry=main, success=False)
    assert relocated[1:] == build(False, entry=main, success=False)[1:]
    assert relocated[0]['validation_units_evaluated'] == 1
    main.write_text(changed)
    build(entry=main, success=False)
    # An earlier-stage failure must retain completed validation products for the next edit.
    main.write_text(changed + '@intrinsic(\n')
    build(entry=main, success=False)
    main.write_text(changed)
    recovered = build(entry=main, success=False)
    assert recovered[1:] == build(False, entry=main, success=False)[1:]
    assert recovered[0]['validation_units_evaluated'] == 0
    assert recovered[0]['validation_units_reused'] > 0
    repaired = changed.replace('to inner: @intrinsic("discard", thing)\n    inner',
                               'to inner {copy}: @intrinsic("discard", copy)\n    inner thing')
    main.write_text(repaired)
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]
    assert not fixed[1]


def check_capture_scope_queries(root, build):
    main = root / 'capture_scope_queries.dl'
    source = ('@intrinsic("store", left, 1)\n@intrinsic("store", right, 2)\n'
              'function first:\n    globals:\n        left\n'
              '    execute: @intrinsic("discard", left)\n'
              'function second:\n    globals:\n        right\n'
              '    execute: @intrinsic("discard", right)\nfirst\nsecond\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[0]['validation_units_evaluated'] == 2
    main.write_text(source.replace('left, 1', 'left, 3'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]['validation_units_evaluated'] == 0
    assert edited[0]['validation_units_reused'] == 2

    # Multiple invalid captures report the first source reference, independently
    # of hash-table insertion order and cache replay.
    main = root / 'capture_source_order.dl'
    source = ('to outer {zulu} and {alpha}:\n    to inner:\n'
              '        @intrinsic("discard", zulu)\n'
              '        @intrinsic("discard", alpha)\n    inner\nouter 1 and 2\n')
    main.write_text(source)
    first = build(entry=main, success=False)
    assert "'zulu'" in first[1]
    reordered = source.replace('        @intrinsic("discard", zulu)\n        @intrinsic("discard", alpha)',
                               '        @intrinsic("discard", alpha)\n        @intrinsic("discard", zulu)')
    main.write_text(reordered)
    second = build(entry=main, success=False)
    assert second[1:] == build(False, entry=main, success=False)[1:]
    assert "'alpha'" in second[1] and "'zulu'" not in second[1]


def check_validation_flow(root, build):
    folder = root / 'validation_flow'
    folder.mkdir()
    main = folder / 'main.dl'
    prefix = ('nothing means: @intrinsic("type", "nothing")\n'
              'flex function {nothing:left} then {nothing:right}:\n    replacement:\n'
              '        @intrinsic("discard", left)\n        @intrinsic("discard", right)\n'
              'flex section maybe condition:\n    replacement: @intrinsic("if", condition)\n'
              'flex section alternatively:\n    replacement: @intrinsic("else")\n'
              'flex section repeat while condition:\n    replacement: @intrinsic("loop while", condition)\n'
              'flex section select value:\n    replacement: @intrinsic("switch", value)\n'
              'flex section option value:\n    replacement: @intrinsic("case", value)\n'
              'flex section other option:\n    replacement: @intrinsic("default case")\n')
    source = prefix + ('@intrinsic("store", {number}, 1) then '
                       '@intrinsic("store", number, @intrinsic("add", number, 2))\n')
    main.write_text(source)
    assert not build(entry=main)[1]
    source = prefix + ('to warn {value}: @intrinsic("store", value, 0) then @intrinsic("discard", value)\n'
                       'warn @intrinsic("command line argument count")\n')
    main.write_text(source)
    assert 'assigned before being read' in build(entry=main)[1]

    def exposed(initializer, selector):
        return prefix + initializer + ('exposed function warn:\n    execute:\n'
            f'        initialize {{scratch}} using {selector} then @intrinsic("discard", scratch)\n'
            'to stable: @intrinsic("discard", 0)\nstable\n')

    initializer = ('function initialize {destination} using {condition}:\n    replacement:\n'
                   '        maybe condition:\n            @intrinsic("store", destination, 1)\n'
                   '        @intrinsic("discard", 0)\n')
    selector = '@intrinsic("equal", @intrinsic("command line argument count"), 1)'
    source = exposed(initializer, selector)
    main.write_text(source)
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    assert 'read before being assigned' in cold[1]
    main.write_text(source.replace('to stable: @intrinsic("discard", 0)', 'to stable: @intrinsic("discard", 1)'))
    reused = build(entry=main)
    assert reused[1:] == build(False, entry=main)[1:]
    assert reused[0]['inference_diagnostic_records_restored'] > 0
    covered = initializer.replace('        @intrinsic("discard", 0)',
              '        alternatively:\n            @intrinsic("store", destination, 2)\n'
              '        @intrinsic("discard", 0)')
    main.write_text(exposed(covered, selector))
    both = build(entry=main)
    assert both[1:] == build(False, entry=main)[1:] and not both[1]
    main.write_text(exposed(initializer.replace('maybe condition:', 'repeat while condition:'), selector))
    loop = build(entry=main)
    assert loop[1:] == build(False, entry=main)[1:]
    assert 'read before being assigned' in loop[1]
    selected = ('function initialize {destination} using {value}:\n    replacement:\n'
                '        select value:\n            option 0:\n                @intrinsic("store", destination, 1)\n'
                '            other option:\n                @intrinsic("store", destination, 2)\n'
                '        @intrinsic("discard", 0)\n')
    main.write_text(exposed(selected, '@intrinsic("command line argument count")'))
    switched = build(entry=main)
    assert switched[1:] == build(False, entry=main)[1:] and not switched[1]


def check_validation_bound_parameters(root, build):
    folder = root / 'validation_bound_parameters'
    folder.mkdir()
    main = folder / 'main.dl'
    setter = 'function assign {target} to {value}:\n    replacement: @intrinsic("store", target, value)\n'
    source = setter + ('to inspect [validation] of {text}:\n    assign validation to 1\n'
                       '    @intrinsic("discard", text)\ninspect validation of "text"\n')
    main.write_text(source)
    assert not build(entry=main)[1], 'A literal header word was treated as an argument'
    source = setter + ('@intrinsic("store", result, 0)\nfunction change [result]:\n    globals: result\n'
                       '    execute: assign result to 1\nchange result\n')
    main.write_text(source)
    assert not build(entry=main)[1], 'A global was treated as an argument'
    source = ('function initialize [with {value}|by default]:\n'
              '    execute: @intrinsic("store", value, 1)\ninitialize by default\n')
    main.write_text(source)
    assert not build(entry=main)[1], 'An omitted choice parameter has no incoming value'
    main.write_text(source + 'initialize with 2\n')
    bound = build(entry=main)
    assert bound[1:] == build(False, entry=main)[1:]
    assert 'assigned before being read' in bound[1]
    main.write_text(source)
    omitted = build(entry=main)
    assert omitted[1:] == build(False, entry=main)[1:] and not omitted[1]
    source = ('class:\n    patterns: tracked\n    members: number\n'
              '    retain: @intrinsic("discard", 0)\n    release: @intrinsic("discard", 0)\n'
              'to overwrite {value}: @intrinsic("store", value, @intrinsic("construct", tracked, 2))\n'
              'overwrite @intrinsic("construct", tracked, 1)\n')
    plain = source.replace('    retain: @intrinsic("discard", 0)\n    release: @intrinsic("discard", 0)\n', '')
    main.write_text(plain)
    assert 'assigned before being read' in build(entry=main)[1]
    main.write_text(source)
    managed = build(entry=main)
    assert managed[1:] == build(False, entry=main)[1:] and not managed[1], 'Replacement releases the incoming managed value'
    main.write_text(plain)
    restored = build(entry=main)
    assert restored[1:] == build(False, entry=main)[1:]
    assert 'assigned before being read' in restored[1]
