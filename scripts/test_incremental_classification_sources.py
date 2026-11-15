"""File-local definition origins participate in incremental classification."""

from test_incremental_program_cache import classification_promotions, read_program


def check_local_pattern_source_move(root, build):
    main = root / "lookup_local_pattern_move.dl"
    helper = root / "lookup_local_patterns.dl"
    header = 'local function:\n    patterns:\n'
    body = '    replacement:\n        9\n'
    tail = 'to outer value: @intrinsic("discard", value)\n@intrinsic("discard", 0)\n'
    imported = header + f'        import {helper.name}\n' + body + tail
    local = header + '        [value]\n' + f'        import {helper.name}\n' + body + tail

    def parameters():
        program = read_program(root / "cache", main)
        path, = [path for path, identity in program["all_function_projection"]["inactive"].items()
                 if identity["header"] == "outer value:"]
        return classification_promotions(root / "cache", program)[path]

    for source, patterns, expected in ((imported, '        [value]\n', {(0, 2)}),
                                       (local, '', set()),
                                       (imported, '        [value]\n', {(0, 2)})):
        main.write_text(source)
        helper.write_text(patterns)
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert parameters() == expected, "a moved local pattern retained a stale parameter classification"
