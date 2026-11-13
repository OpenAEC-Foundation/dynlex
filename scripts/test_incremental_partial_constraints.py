"""A failed constraint keeps completed, failed and unvisited query products distinct."""


def check_partial_constraint_products(root, build):
    for broken_first in (False, True):
        main = root / f"partial_constraints_{broken_first}.dl"
        healthy = 'to healthy {good type:value}: @intrinsic("discard", value)\n'
        broken = 'to broken {bad type:value}: @intrinsic("discard", value)\n'
        source = ('good type means: @intrinsic("type", "int", 32)\n'
                  'bad type means: 0\n'
                  + (broken + healthy if broken_first else healthy + broken)
                  + 'healthy 1\n'
                  'to unused: @intrinsic("discard", 0)\n')
        main.write_text(source)
        failed = build(entry=main, success=False)
        assert failed[1:] == build(False, entry=main, success=False)[1:]
        assert failed[0]["constraint_probes_run"] == (1 if broken_first else 2)

        edited = source.replace('"discard", 0', '"discard", 2')
        main.write_text(edited)
        repeated = build(entry=main, success=False)
        assert repeated[1:] == build(False, entry=main, success=False)[1:]
        assert repeated[0]["constraint_probes_run"] == 0, \
            "an unrelated edit repeated a completed or failed constraint query"
        assert repeated[0]["constraint_values_reused"] == (0 if broken_first else 1)
        assert repeated[0]["constraint_probes_reused"] == (1 if broken_first else 2)

        fixed_source = edited.replace('bad type means: 0',
                                      'bad type means: @intrinsic("type", "int", 32)')
        main.write_text(fixed_source)
        fixed = build(entry=main)
        assert fixed[1:] == build(False, entry=main)[1:]
        assert fixed[0]["constraint_probes_run"] == (2 if broken_first else 1), \
            "correcting one query repeated an independent completed constraint"
        assert fixed[0]["constraint_values_reused"] == (0 if broken_first else 1)

        main.write_text(edited)
        broken_again = build(entry=main, success=False)
        assert broken_again[1:] == build(False, entry=main, success=False)[1:]
        assert broken_again[0]["constraint_probes_run"] == 1
        changed_healthy = edited.replace('"int", 32', '"int", 64')
        main.write_text(changed_healthy)
        still_broken = build(entry=main, success=False)
        assert still_broken[1:] == build(False, entry=main, success=False)[1:]
        assert still_broken[0]["constraint_probes_run"] == (0 if broken_first else 1)


def check_partial_constraint_later_pass(root, build):
    main = root / "partial_constraints_later_pass.dl"
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'to healthy {healthy type from 1:value}: @intrinsic("discard", value)\n'
              'to broken {broken type from 1:value}: @intrinsic("discard", value)\n'
              'function healthy type from {integer:value}:\n'
              '    execute: @intrinsic("return", @intrinsic("type", "int", 32))\n'
              'function broken type from {integer:value}:\n'
              '    execute: @intrinsic("return", 0)\n'
              'healthy 1\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('"return", 0', '"return", @intrinsic("type", "int", 32)'))
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]
    assert fixed[0]["constraint_values_reused"] == 3
    # Only the repaired query probes: it first defers until its provider's
    # constraint is installed, then succeeds in the second pass.
    assert fixed[0]["constraint_probes_run"] == 2


def check_partial_constraint_context(root, build):
    folder = root / "partial_constraint_context"
    folder.mkdir()
    main, config = folder / "main.dl", folder / "config.dl"
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'broken type means: 0\n'
              'to healthy {integer:value}: @intrinsic("discard", value)\n'
              'to broken {broken type:value}: @intrinsic("discard", value)\n')
    wording = ('dynlex options:\n    messages:\n'
               '        unknown type constraint: "First constraint wording: {type_constraint}"\n')
    main.write_text(source)
    config.write_text(wording)
    failed = build(entry=main, success=False)
    assert "First constraint wording" in failed[1]
    config.write_text(wording.replace("First", "Second"))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:]
    assert "Second constraint wording" in changed[1]
    assert changed[0]["frontend_reused"] and changed[0]["constraint_probes_run"] == 0
    assert changed[0]["objects_built"] == 0 and changed[0]["link_ms"] == 0

    edited = 'to earlier: @intrinsic("discard", 0)\n' + source
    main.write_text(edited)
    moved = build(entry=main, success=False)
    assert moved[1:] == build(False, entry=main, success=False)[1:]
    assert moved[0]["constraint_probes_run"] == 0
    assert "Second constraint wording" in moved[1]

    main.write_text(edited + 'to invalid {broken:name:extra}: @intrinsic("discard", 1)\n')
    earlier_failure = build(entry=main, success=False)
    assert earlier_failure[1:] == build(False, entry=main, success=False)[1:]
    assert earlier_failure[0]["constraint_probes_run"] == 0
    main.write_text(edited.replace('"discard", 0', '"discard", 1'))
    recovered = build(entry=main, success=False)
    assert recovered[1:] == build(False, entry=main, success=False)[1:]
    assert recovered[0]["constraint_probes_run"] == 0, \
        "an intervening parse error discarded unaffected constraint products"


def check_partial_constraint_class(root, build):
    main = root / "partial_constraint_class.dl"
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'class:\n    patterns:\n        a node\n    members:\n        value as integer\n'
              'to healthy {a node:value}: @intrinsic("discard", value)\n'
              'broken type means: 0\n'
              'to broken {broken type:value}: @intrinsic("discard", value)\n'
              'healthy (@intrinsic("construct", a node, @intrinsic("cast", 1, integer)))\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "int", 32), 1, "%lld\\n", @intrinsic("size of", '
              '@intrinsic("type of", @intrinsic("construct", a node, @intrinsic("cast", 1, integer))))))\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('"int", 32', '"int", 64'))
    wider = build(entry=main, success=False)
    assert wider[1:] == build(False, entry=main, success=False)[1:]
    assert wider[0]["constraint_probes_run"] == 1
    assert wider[0]["constraint_probes_reused"] == 1
    main.write_text(main.read_text().replace('broken type means: 0', 'broken type means: integer'))
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]
    assert fixed[2] == b"8\n"
    assert fixed[0]["constraint_probes_run"] == 1
    assert fixed[0]["constraint_values_reused"] == 1
    main.write_text(source)
    broken_after_native = build(entry=main, success=False)
    assert broken_after_native[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source.replace('broken type means: 0', 'broken type means: integer'))
    narrower = build(entry=main)
    assert narrower[1:] == build(False, entry=main)[1:]
    assert narrower[2] == b"4\n", "a partial class snapshot retained the older native layout"


def check_partial_constraint_cycle(root, build):
    main = root / "partial_constraint_cycle.dl"
    source = ('left {right 1:value} means: @intrinsic("type", "int", 32)\n'
              'right {left 1:value} means: @intrinsic("type", "int", 32)\n'
              'to unused: @intrinsic("discard", 0)\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    edited = source.replace('"discard", 0', '"discard", 1')
    main.write_text(edited)
    repeated = build(entry=main, success=False)
    assert repeated[1:] == build(False, entry=main, success=False)[1:]
    assert repeated[0]["constraint_probes_run"] == 0

    # A cycle is established by a complete pass, after every independent
    # constraint has had its ordinary opportunity to report an error.
    independent = edited + 'to broken {0:value}: @intrinsic("discard", value)\n'
    main.write_text(independent)
    added_failure = build(entry=main, success=False)
    assert added_failure[1:] == build(False, entry=main, success=False)[1:], \
        "a cached cycle hid a newly added independent constraint error"
    main.write_text(independent.replace('"discard", 1', '"discard", 2'))
    repeated_failure = build(entry=main, success=False)
    assert repeated_failure[1:] == build(False, entry=main, success=False)[1:]
    assert repeated_failure[0]["constraint_probes_run"] == 0

    # An independent successful query can add another pass. Previously
    # blocked queries remain blocked until one of their inputs changes.
    main.write_text(independent.replace('{0:value}', '{@intrinsic("type", "int", 32):value}'))
    added_success = build(entry=main, success=False)
    assert added_success[1:] == build(False, entry=main, success=False)[1:]
    assert added_success[0]["constraint_probes_run"] == 1
    main.write_text(edited.replace('right {left 1:value}', 'right {value}'))
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]


def check_partial_constraint_import(root, build):
    main = root / "partial_constraint_import.dl"
    helper = root / "partial_constraint_provider.dl"
    helper.write_text('broken type means: 0\n')
    main.write_text('import partial_constraint_provider.dl\n'
                    'integer means: @intrinsic("type", "int", 32)\n'
                    'to healthy {integer:value}: @intrinsic("discard", value)\n'
                    'to broken {broken type:value}: @intrinsic("discard", value)\n'
                    'healthy 1\n')
    initial = build(entry=main, success=False)
    assert initial[1:] == build(False, entry=main, success=False)[1:]
    helper.write_text('broken type means: 1\n')
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    assert edited[0]["constraint_probes_run"] == 1
    assert edited[0]["constraint_values_reused"] == 1
    helper.write_text('broken type means: @intrinsic("type", "int", 32)\n')
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]
    assert fixed[0]["constraint_probes_run"] == 1
    assert fixed[0]["constraint_values_reused"] == 1


def check_failed_constraint_frontend(root, build):
    main = root / "failed_constraint_frontend.dl"
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'to stable value: @intrinsic("discard", value)\n'
              'to broken {0:value}: @intrinsic("discard", value)\n'
              'to edited: @intrinsic("discard", 0)\n'
              'stable 1\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    edited = source.replace('to edited: @intrinsic("discard", 0)',
                            'to edited: @intrinsic("discard", 1)')
    main.write_text(edited)
    repeated = build(entry=main, success=False)
    assert repeated[1:] == build(False, entry=main, success=False)[1:]
    assert repeated[0]["classification_units_reused"] > 0, \
        "a constraint failure discarded completed frontend query products"
    assert repeated[0]["constraint_probes_run"] == 0
    main.write_text(edited.replace('to stable value: @intrinsic("discard", value)',
                                   'to stable value: @intrinsic("discard", 0)'))
    reclassified = build(entry=main, success=False)
    assert reclassified[1:] == build(False, entry=main, success=False)[1:], \
        "frontend reuse hid removal of an implicit parameter's last use"
    main.write_text(main.read_text().replace('to broken {0:value}', 'to broken {integer:value}'))
    no_longer_a_parameter = build(entry=main, success=False)
    assert no_longer_a_parameter[1:] == build(False, entry=main, success=False)[1:], \
        "repairing the constraint retained a stale implicit-parameter interface"


def check_constraint_diagnostic_publication(root, build):
    main = root / "constraint_diagnostic_publication.dl"
    source = ('function return value:\n    replacement: @intrinsic("return", value)\n'
              'to get left - right:\n    return @intrinsic("subtract", left, right)\n'
              'to get value adjusted:\n    return value\n'
              'to get the tested form of value:\n    return value - 1 adjusted\n'
              'function [tested type]:\n    execute:\n'
              '        @intrinsic("discard", the tested form of 4)\n'
              '        @intrinsic("return", @intrinsic("type", "int", 32))\n'
              'to consume {tested type:value}: @intrinsic("discard", value)\n'
              'consume 1\n@intrinsic("discard", tested type)\n'
              'to unused: @intrinsic("discard", 0)\n')
    main.write_text(source)
    first = build(entry=main)
    assert first[1:] == build(False, entry=main)[1:]
    assert first[1].count("Warning:") == 1
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:], \
        "a retained constraint diagnostic changed publication order or multiplicity"

    first = 'to first {tested type:value}: @intrinsic("discard", value)\n'
    second = 'to second {tested type:value}: @intrinsic("discard", value)\n'
    shared = source.replace('to consume {tested type:value}: @intrinsic("discard", value)\n',
                            first + second).replace('consume 1\n', 'second 1\n')
    shared = shared.replace('@intrinsic("discard", tested type)\n', '')
    main.write_text(shared)
    initial_shared = build(entry=main)
    assert initial_shared[1:] == build(False, entry=main)[1:]
    assert initial_shared[1].count("Warning:") == 1
    main.write_text(shared.replace(first, ''))
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:], \
        "removing the first query lost a warning suppressed in a retained query"
    assert removed[0]["constraint_values_reused"] == 1
