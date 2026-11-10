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
