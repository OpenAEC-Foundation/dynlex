"""Constraint queries retain unrelated results regardless of declaration order."""

from test_incremental_program_cache import read_program


def check_constraint_query_units(root, build):
    check_later_pass_constraint_query(root, build)
    for dependent_first in (False, True):
        main = root / f"constraint_query_units_{dependent_first}.dl"
        dependent = 'to consume changed {changing type:value}: @intrinsic("discard", value)\n'
        independent = 'to consume stable {stable type:value}: @intrinsic("discard", value)\n'
        source = ('changing type means: @intrinsic("type", "int", 32)\n'
                  'stable type means: @intrinsic("type", "float", 64)\n'
                  + (dependent + independent if dependent_first else independent + dependent)
                  + 'consume changed (@intrinsic("cast", @intrinsic("command line argument count"), changing type))\n'
                  'consume stable 1.0\n')
        main.write_text(source)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        for width in (64, 32):
            main.write_text(source.replace('"int", 32', f'"int", {width}'))
            edited = build(entry=main)
            assert edited[1:] == build(False, entry=main)[1:]
            assert edited[0]["constraint_probes_reused"] == 1, \
                "an earlier changed constraint invalidated an independent later query"
            assert edited[0]["constraint_values_reused"] == 1
            assert edited[0]["constraint_expressions_reused"] == 1
            assert edited[0]["constraint_probes_run"] == 1


def check_constraint_signature_dependencies(root, build):
    main = root / "constraint_signature_dependencies.dl"
    source = ('kind means: @intrinsic("type", "int", 32)\n'
              'function type from {kind:value}:\n'
              '    execute: @intrinsic("return", @intrinsic("type", "float", 64))\n'
              'to consume {type from 1:value}: @intrinsic("discard", value)\n'
              'consume 1.0\n')
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('"type", "int", 32', '"type", "bool"'))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:], \
        "a type-provider signature change retained a downstream constraint result"
    assert changed[0]["constraint_values_reused"] == 0


def check_later_pass_constraint_query(root, build):
    main = root / "later_pass_constraint_query.dl"
    source = ('changing type means: @intrinsic("type", "int", 32)\n'
              'stable integer means: @intrinsic("type", "int", 32)\n'
              'to consume stable {a type from 1:value}: @intrinsic("discard", value)\n'
              'function a type from {stable integer:value}:\n'
              '    execute: @intrinsic("return", @intrinsic("type", "float", 64))\n'
              'to consume changed {changing type:value}: @intrinsic("discard", value)\n'
              'consume stable 1.0\n'
              'consume changed (@intrinsic("cast", @intrinsic("command line argument count"), changing type))\n')
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    schedule = read_program(root / "cache", main)["all_function_projection"]["constraint_probe_schedule"]
    assert schedule["passes"][0] > 1, "the regression needs a deferred constraint query"
    main.write_text(source.replace('"int", 32)\nstable', '"int", 64)\nstable'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["constraint_values_reused"] == 2, \
        "a later-pass query lost its independent completed result"
    assert edited[0]["constraint_probes_reused"] == 2
    assert edited[0]["constraint_expressions_reused"] == 2
    assert edited[0]["constraint_probes_run"] == 1


def check_constraint_declaration_edits(root, build):
    main = root / "constraint_declaration_edits.dl"
    source = ('stable type means: @intrinsic("type", "float", 64)\n'
              'changing type means: @intrinsic("type", "int", 32)\n'
              'to consume stable {stable type:value}: @intrinsic("discard", value)\n'
              'to consume edited {changing type:value}: @intrinsic("discard", value)\n'
              'consume stable 1.0\nconsume edited 1\n')
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    renamed = source.replace('edited {changing type:value}: @intrinsic("discard", value)',
                             'edited {changing type:item}: @intrinsic("discard", item)')
    main.write_text(renamed)
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]["constraint_probes_run"] == 1
    assert changed[0]["constraint_values_reused"] == 1, \
        "a parameter rename invalidated an unrelated declaration constraint"
    extra = 'to unused {stable type:unused}: @intrinsic("discard", unused)\n'
    main.write_text(renamed.replace('to consume stable', extra + 'to consume stable'))
    added = build(entry=main)
    assert added[1:] == build(False, entry=main)[1:]
    assert added[0]["constraint_probes_run"] == 1
    assert added[0]["constraint_values_reused"] == 2, \
        "a new query shifted and invalidated existing constraint records"
    main.write_text(renamed)
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert removed[0]["constraint_probes_run"] == 0
    assert removed[0]["type_inference_reused"] or removed[0]["constraint_values_reused"] == 2
    main.write_text(''.join(line for line in renamed.splitlines(keepends=True)
                            if 'consume edited' not in line))
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert removed[0]["constraint_probes_run"] == 0
    assert removed[0]["type_inference_reused"] or removed[0]["constraint_values_reused"] == 1


def check_constraint_provider_membership(root, build):
    main = root / "constraint_provider_membership.dl"
    source = ('floating means: @intrinsic("type", "float", 64)\n'
              'integer means: @intrinsic("type", "int", 32)\n'
              'to stable {floating:value}: @intrinsic("discard", value)\n'
              'to changed {integer:value}: @intrinsic("discard", value)\n'
              'stable 1.0\nchanged 1\n')
    for remove in (False, True):
        main.write_text(source)
        build(entry=main)
        changed = (source.replace('integer means: @intrinsic("type", "int", 32)\n', '') if remove
                   else source + 'function [integer]:\n    execute: @intrinsic("return", @intrinsic("type", "float", 64))\n')
        main.write_text(changed)
        edited = build(entry=main, success=False)
        assert edited[1:] == build(False, entry=main, success=False)[1:]
        assert edited[0]["constraint_values_reused"] == 1, \
            "provider membership changes invalidated an unrelated type query"
        assert edited[0]["constraint_probes_run"] >= 1


def check_constraint_provider_visibility(root, build):
    main = root / "constraint_provider_visibility.dl"
    helper = root / "constraint_private_provider.dl"
    helper.write_text('to marker: @intrinsic("discard", 0)\n')
    main.write_text('import constraint_private_provider.dl\n'
                    'local flex function [integer]:\n    replacement: @intrinsic("type", "int", 32)\n'
                    'to consume {integer:value}: @intrinsic("discard", value)\n'
                    'consume 1\n')
    build(entry=main)
    helper.write_text(helper.read_text() + 'local function [integer]:\n'
                      '    execute: @intrinsic("return", @intrinsic("type", "float", 64))\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["constraint_probes_run"] == 0
    assert edited[0]["constraint_values_reused"] == 1, \
        "a hidden file-local provider invalidated a query in another file"


def check_unbound_constraint_argument(root, build):
    main = root / "unbound_constraint_argument.dl"
    source = ('type for amount means: @intrinsic("type", "int", amount)\n'
              'to consume {type for missing:value}: @intrinsic("discard", value)\n'
              'consume 1\n')
    main.write_text(source)
    failed = build(entry=main, success=False)
    assert "Unknown variable: missing" in failed[1]
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    repeated = build(entry=main, success=False)
    assert repeated[0]["frontend_reused"]
    assert repeated[1:] == failed[1:]
    main.write_text(source + 'missing means: 32\n')
    fixed = build(entry=main)
    assert fixed[1:] == build(False, entry=main)[1:]

    for label, expression in (("intrinsic", '@intrinsic("type of", {missing})'),
                              ("helper", 'type for {missing}')):
        main = root / f"unbound_explicit_constraint_{label}.dl"
        source = ('type for amount means: @intrinsic("type", "int", amount)\n'
                  f'to consume {{{expression}:value}}: @intrinsic("discard", value)\n')
        for name in ("missing", "absent"):
            main.write_text(source.replace("missing", name))
            failed = build(entry=main, success=False)
            assert f"Unknown variable: {name}" in failed[1]
            assert failed[1:] == build(False, entry=main, success=False)[1:]
            repeated = build(entry=main, success=False)
            assert repeated[0]["frontend_reused"]
            assert repeated[1:] == failed[1:]
