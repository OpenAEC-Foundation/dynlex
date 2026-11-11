"""A body edit can change an entire recursive signature dependency cycle."""

from test_incremental_program_cache import read_program


def check_recursive_classification_cascade(root, build):
    main = root / "recursive_classification.dl"
    source = ('to alpha value:\n'
              '    @intrinsic("discard", value)\n'
              '    beta value\n'
              'to beta value:\n'
              '    alpha value\n'
              '@intrinsic("discard", 0)\n')

    def promoted_parameters():
        declarations = read_program(root / "cache", main)["all_function_projection"]["function_declarations"]
        # The compact declaration record stores the promotion flag in slot 4.
        return {definition["spelling"]: [element[1] for element in definition["elements"] if element[4]]
                for declaration in declarations.values() for definition in declaration["definitions"]}

    main.write_text(source)
    build(entry=main)
    expected = {"alpha value": ["value"], "beta value": ["value"]}
    assert promoted_parameters() == expected
    removed = source.replace('    @intrinsic("discard", value)\n', '')
    main.write_text(removed)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    assert promoted_parameters() == {"alpha value": [], "beta value": []}, \
        "a cached recursive signature sustained a removed parameter"
    main.write_text(removed + 'alpha 1\n')
    failed = build(entry=main, success=False)
    assert failed[1], "the invalid call produced no diagnostic"
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    assert promoted_parameters() == expected


def check_classification_cycle_exports(root, build):
    main = root / "classification_cycle_exports.dl"
    source = (
        'flex function [alpha] value:\n'
        '    replacement:\n'
        '        @intrinsic("discard", value)\n'
        '        beta value\n'
        'flex function [beta] value:\n'
        '    replacement:\n'
        '        alpha value\n'
        'to observer value: alpha value\n'
        '[keep] item means: item\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", value', '"discard", @intrinsic("add", value, 1)'))
    stable_cycle = build(entry=main)
    assert stable_cycle[0]["classification_parameters_reused"] >= 2, "unchanged cycle exports reclassified a reader"
    assert stable_cycle[0]["classification_units_reactivated"] == 0
    assert stable_cycle[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('        @intrinsic("discard", value)\n', ''))
    changed_cycle = build(entry=main)
    assert changed_cycle[0]["classification_units_reactivated"] >= 1
    assert changed_cycle[0]["classification_parameters_reused"] == 1
    assert changed_cycle[1:] == build(False, entry=main)[1:]

    main = root / "classification_deleted_provider.dl"
    helper = root / "classification_deleted_helper.dl"
    source = 'value means: 5\nto outer value: @intrinsic("discard", value)\n'
    helper.write_text(source)
    main.write_text('import classification_deleted_helper.dl\n'
                    'to final value: outer value\n'
                    '@intrinsic("discard", 0)\n')
    build(entry=main)
    helper.write_text(source.removeprefix('value means: 5\n'))
    deleted = build(entry=main)
    assert deleted[0]["classification_units_reactivated"] >= 1, "a deleted provider lost its cached readers"
    assert deleted[1:] == build(False, entry=main)[1:]

def check_incremental_classification(root, build):
    main = root / "independent_classification.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [first] value:\n'
        '    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'function [second] item:\n'
        '    execute:\n'
        '        @intrinsic("return", @intrinsic("add", item, 1))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", first 7))\n'
        '@intrinsic("discard", second 8)\n'
    )
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"8\n"
    for number in (2, 3):
        main.write_text(source.replace('"add", item, 1', f'"add", item, {number}'))
        edited = build(entry=main)
        assert edited[0]["classification_parameters_reused"] >= 1, "independent implicit parameter was rediscovered"
        assert edited[0]["classification_units_invalidated"] >= 1
        assert edited[0]["cached_body_resolution_references_skipped"] >= 1
        assert edited[0]["body_resolution_reference_visits"] < cold[0]["body_resolution_reference_visits"]
        assert edited[1:] == build(False, entry=main)[1:]

    commented = main.read_text().replace('        @intrinsic("return", @intrinsic("add", value, 1))',
                                         '        # Keep the promotion location after this comment.\n'
                                         '        @intrinsic("return", @intrinsic("add", value, 1))')
    main.write_text(commented)
    assert build(entry=main)[0]["frontend_reused"]
    main.write_text(commented.replace('"add", item, 3', '"add", item, 4'))
    after_comment = build(entry=main)
    assert after_comment[0]["classification_parameters_reused"] >= 1
    assert after_comment[1:] == build(False, entry=main)[1:]

    main.write_text(main.read_text().replace('second 8)', 'second 9)'))
    root_edit = build(entry=main)
    assert root_edit[0]["classification_parameters_reused"] >= 2, "root execution repeated unchanged parameter classification"
    assert root_edit[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + '@intrinsic("store", {state}, 1)\n')
    root_binding = build(entry=main)
    assert root_binding[0]["classification_parameters_reused"] == 0, "new root names reused an earlier classification environment"
    assert root_binding[1:] == build(False, entry=main)[1:]

    main = root / "classification_cycle.dl"
    source = (
        'flex function [alpha] value:\n'
        '    replacement:\n'
        '        @intrinsic("discard", value)\n'
        '        beta value\n'
        'flex function [beta] value:\n'
        '    replacement:\n'
        '        alpha value\n'
        'to idle: @intrinsic("discard", 0)\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    build(entry=main)
    unrelated = source.replace('to idle: @intrinsic("discard", 0)', 'to idle: @intrinsic("discard", 1)')
    main.write_text(unrelated)
    reused = build(entry=main)
    assert reused[0]["classification_parameters_reused"] >= 2, "an unchanged classification cycle was recomputed"
    assert reused[1:] == build(False, entry=main)[1:]
    projection = read_program(root / "cache", main)["all_function_projection"]
    before = projection["matcher_projection"]["declaration_digests"]
    cycle_paths = {path for path, identity in projection["inactive"].items()
                   if "alpha" in identity["header"] or "beta" in identity["header"]}
    assert len(cycle_paths) == 2
    main.write_text(unrelated.replace('        @intrinsic("discard", value)\n', ''))
    removed = build(entry=main)
    assert removed[0]["classification_parameters_reused"] == 0, "a dirty cycle retained its old parameters"
    assert removed[0]["classification_units_invalidated"] >= 2
    assert removed[1:] == build(False, entry=main)[1:]
    after = read_program(root / "cache", main)["all_function_projection"]["matcher_projection"]["declaration_digests"]
    assert all(after[path] != before[path] for path in cycle_paths), "a classification cycle sustained a removed parameter"
    main.write_text(main.read_text() + 'alpha 7\n')
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]

    main = root / "classification_failed_lookup.dl"
    source = (
        '[identity] value means: value\n'
        '@intrinsic("discard", identity 7)\n'
    )
    main.write_text(source)
    build(entry=main)
    main.write_text(source + 'value means: 9\n')
    added = build(entry=main, success=False)
    assert added[1:] == build(False, entry=main, success=False)[1:], "a new pattern retained a failed classification lookup"
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "classification_data_metadata.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'section [answer] {first} [or] {second}:\n'
        '    declares: second\n'
        '    result: integer\n'
        '    value:\n'
        '        42\n'
        '[selected] {value} means: @intrinsic("add", value, 1)\n'
        '[other] {value} means: value\n'
        '[read] selected name means: selected name\n'
        '@intrinsic("store", name, 7)\n'
        'answer selected name or other name:\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", read selected name))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"8\n"
    main.write_text(source.replace('"%d\\n"', '"%d!\\n"'))
    unrelated_root = build(entry=main)
    assert unrelated_root[2] == b"8!\n"
    assert unrelated_root[0]["classification_parameters_reused"] >= 1, "an unrelated root edit invalidated data-name dependencies"
    assert unrelated_root[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('declares: second', 'declares: first'))
    metadata = build(entry=main)
    assert metadata[2] == b"42\n", "declares metadata retained an earlier parameter classification"
    assert metadata[0]["classification_parameters_reused"] == 0
    assert metadata[1:] == build(False, entry=main)[1:]
    current = main.read_text()
    shadow = ('flex section [answer] [selected] [name] [or] [other] [name]:\n'
              '    replacement:\n'
              '        @intrinsic("discard", 0)\n')
    main.write_text(current + shadow)
    overridden = build(entry=main)
    assert overridden[2] == b"8\n", "a new competing section retained a declared data name"
    assert overridden[1:] == build(False, entry=main)[1:]
    main.write_text(current)
    restored = build(entry=main)
    assert restored[2] == b"42\n", "removing a competing section lost its data-name dependency"
    assert restored[1:] == build(False, entry=main)[1:]

    main = root / "classification_local_visibility.dl"
    helper = root / "classification_local_helper.dl"
    helper_source = '[helper] value means: value\n'
    helper.write_text(helper_source)
    main.write_text(
        'import classification_local_helper.dl\n'
        'integer means: @intrinsic("type", "int")\n'
        '[first] value means: @intrinsic("add", value, 1)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", first 7))\n'
    )
    assert build(entry=main)[2] == b"8\n"
    private_value = 'local function [value]:\n    execute:\n        @intrinsic("return", 9)\n'
    helper.write_text(helper_source + private_value)
    private = build(entry=main)
    assert private[2] == b"8\n"
    assert private[0]["classification_parameters_reused"] >= 1, "an invisible local pattern invalidated a header"
    assert private[1:] == build(False, entry=main)[1:]
    helper.write_text(helper_source + private_value.removeprefix('local '))
    public = build(entry=main, success=False)
    assert public[1:] == build(False, entry=main, success=False)[1:]
    helper.write_text(helper_source + private_value)
    recovered = build(entry=main)
    assert recovered[2] == b"8\n"
    assert recovered[1:] == build(False, entry=main)[1:]
    check_classification_propagation(root, build)


def check_classification_propagation(root, build):
    main = root / "classification_propagation.dl"
    source = (
        'to core value: @intrinsic("discard", value)\n'
        'to outer value: core value\n'
        'to final value: outer value\n'
        '[neighbor] other means: @intrinsic("add", other, 1)\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    build(entry=main)
    edited = source.replace('"discard", value', '"discard", @intrinsic("add", value, 1)')
    main.write_text(edited)
    stable = build(entry=main)
    assert stable[0]["classification_parameters_reused"] >= 3, "an unchanged signature reclassified its callers"
    assert stable[0]["classification_units_reactivated"] == 0
    assert stable[1:] == build(False, entry=main)[1:]
    before = read_program(root / "cache", main)["all_function_projection"]["function_declarations"]
    main.write_text(source.replace('"discard", value', '"discard", 0'))
    changed = build(entry=main)
    assert changed[0]["classification_units_reactivated"] >= 2, "a changed signature retained cached callers"
    assert changed[0]["classification_parameters_reused"] == 1
    assert changed[0]["variable_like_count_references_scanned"] == 0, "a restarted unchanged caller rescanned its words"
    assert changed[1:] == build(False, entry=main)[1:]
    projection = read_program(root / "cache", main)["all_function_projection"]
    after = projection["function_declarations"]
    chain = {path for path, identity in projection["inactive"].items()
             if any(word in identity["header"] for word in ("core", "outer", "final"))}
    assert len(chain) == 3
    assert all(before[path] != after[path] for path in chain), "a last-use removal did not cascade through every caller"
    assert all(not element[4] for path in chain for definition in after[path]["definitions"]
               for element in definition["elements"]), "a caller kept an implicit parameter after its last use vanished"
    main.write_text(main.read_text() + 'final 7\n')
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    check_classification_cycle_exports(root, build)

    main = root / "classification_matcher_tree.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              '[choose] item means: @intrinsic("add", item, 1)\n'
              '@intrinsic("discard", choose 7)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source + 'implicitly convert {integer:value}:\n    replacement:\n        value\n')
    conversion = build(entry=main)
    assert conversion[0]["classification_parameters_reused"] >= 1, "a conversion invalidated ordinary pattern lookups"
    assert conversion[0]["classification_units_reactivated"] == 0
    assert conversion[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + '[add] {payload} means: payload\n')
    intrinsic_name = build(entry=main)
    assert intrinsic_name[0]["classification_parameters_reused"] >= 1, "a quoted intrinsic name became a lookup dependency"
    assert intrinsic_name[0]["classification_units_reactivated"] == 0
    assert intrinsic_name[1:] == build(False, entry=main)[1:]

    main = root / "classification_capture_capacity.dl"
    source = ('function first second:\n'
              '    execute:\n'
              '        @intrinsic("discard", first)\n'
              '        @intrinsic("discard", second)\n'
              '[identity] value means: value\n'
              '@intrinsic("discard", identity 7)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", second', '"discard", @intrinsic("add", second, 1)'))
    capacity = build(entry=main)
    assert capacity[0]["classification_units_potentially_invalidated"] == 1, "two captures could match one input token"
    assert capacity[1:] == build(False, entry=main)[1:]

    main = root / "classification_zero_width.dl"
    source = '[identity] value means: value\n@intrinsic("discard", identity 7)\n'
    empty = 'function [optional|]:\n    replacement:\n        1\n'
    main.write_text(source)
    build(entry=main)
    main.write_text(source + empty)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(source + empty + '@intrinsic("discard", 1)\n')
    zero_width = build(entry=main)
    assert zero_width[0]["classification_parameters_reused"] >= 1
    assert zero_width[1:] == build(False, entry=main)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
