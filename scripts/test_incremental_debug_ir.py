"""Debug IR reuse proves authored locations independently of runtime inputs."""


def check_debug_ir_units(root, build):
    main = root / "debug_ir_units.dl"
    source = ('to get first {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              'to get second {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 2))\n'
              '@intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d %d\\n", '
              'first @intrinsic("command line argument count"), '
              'second @intrinsic("command line argument count"))\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    assert build(entry=main, flags=("-g",))[2] == b"1 2\n"
    changed = source.replace('"discard", 0', '"discard", 1')
    for text, minimum_reused in ((changed, 2),
                                 (changed.replace('to get second', '# moved second\nto get second'), 1),
                                 ('# moved all\n' + changed, 0)):
        main.write_text(text)
        edited = build(entry=main, flags=("-g",))
        assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
        assert edited[0]["functions_ir_reused"] >= minimum_reused, edited[0]
        assert edited[0]["debug_ir_location_queries"] > 0, edited[0]
        if minimum_reused == 2:
            assert edited[0]["inference_bodies_loaded"] == 0, edited[0]
    main = root / "debug_ir_globals.dl"
    for index, text in enumerate(debug_ir_global_edits()):
        main.write_text(text)
        edited = build(entry=main, flags=("-g",))
        assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
        assert edited[2] == b"1\n"
        if index:
            assert edited[0]["functions_ir_reused"] > 0, edited[0]
    from test_debug_source_origins import cross_file_sources
    helper, source = cross_file_sources(root)
    main = root / "debug_ir_imported_binding.dl"
    main.write_text(source)
    build(entry=main, flags=("-g",))
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    edited = build(entry=main, flags=("-g",))
    assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
    assert edited[0]["functions_ir_reused"] > 0, edited[0]
    helper.write_text("# moved optional declaration\n" + helper.read_text())
    moved = build(entry=main, flags=("-g",))
    assert moved[1:] == build(False, entry=main, flags=("-g",))[1:]
    assert not moved[0]["functions_ir_reused"], moved[0]


def debug_ir_global_edits():
    source = ('integer means: @intrinsic("type", "int")\n'
              'class:\n    patterns:\n        record\n    members:\n        value as integer\n'
              '# spare location\n'
              'to get independent {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              'function [read global]:\n    globals:\n        shared\n    execute:\n'
              '        @intrinsic("return", shared\'s value)\n'
              '@intrinsic("store", shared, @intrinsic("construct", record, '
              '@intrinsic("command line argument count")))\n'
              '@intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", '
              '@intrinsic("add", read global, independent @intrinsic("command line argument count")))\n'
              '@intrinsic("discard", 0)\n')
    changed = source.replace('"discard", 0', '"discard", 1')
    return (source, changed,
            changed.replace('@intrinsic("store", shared', '# moved global\n@intrinsic("store", shared'),
            changed.replace('class:\n', '# moved class\nclass:\n').replace('# spare location\n', ''),
            changed.replace('patterns:\n        record', 'patterns:\n        renamed record')
                   .replace('"construct", record,', '"construct", renamed record,'))


def debug_ir_recursive_edits():
    source = ('integer means: @intrinsic("type", "int")\n'
              'pointer to kind means: @intrinsic("add pointer depth", kind)\n'
              'class:\n    patterns:\n        node\n    members:\n'
              '        next as pointer to node\n        value as integer\n'
              'to get element {row}:\n    @intrinsic("return", row\'s value)\n'
              '@intrinsic("store", row, @intrinsic("construct", node))\n'
              '@intrinsic("store", row\'s value, @intrinsic("command line argument count"))\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), '
              '1, "%d\\n", element row)\n@intrinsic("discard", 0)\n')
    return source, source.replace('"discard", 0', '"discard", 1')
