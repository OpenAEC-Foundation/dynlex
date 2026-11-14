"""File-local definition origins participate in incremental classification."""

from test_incremental_program_cache import read_program


def check_local_pattern_source_move(root, build):
    main = root / "lookup_local_pattern_move.dl"
    helper = root / "lookup_local_patterns.dl"
    header = 'local function:\n    patterns:\n'
    body = '    replacement:\n        9\n'
    tail = 'to outer value: @intrinsic("discard", value)\n@intrinsic("discard", 0)\n'
    imported = header + f'        import {helper.name}\n' + body + tail
    local = header + '        [value]\n' + f'        import {helper.name}\n' + body + tail

    def parameters():
        declarations = read_program(root / "cache", main)["all_function_projection"]["function_declarations"]
        return [element[1] for unit in declarations.values() for definition in unit["definitions"]
                if definition["spelling"] == "outer value"
                for element in definition["elements"] if element[4]]

    for source, patterns, expected in ((imported, '        [value]\n', ["value"]),
                                       (local, '', []),
                                       (imported, '        [value]\n', ["value"])):
        main.write_text(source)
        helper.write_text(patterns)
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert parameters() == expected, "a moved local pattern retained a stale parameter classification"
