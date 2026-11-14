"""File preparation is reusable independently of import and semantic queries."""


def check_source_preparation(root, build):
    project = root / "prepared-sources"
    project.mkdir()
    main, value, unused = (project / name for name in ("main.dl", "value.dl", "unused.dl"))
    source = ('import unused.dl\nimport value.dl\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "puts", '
              '@intrinsic("type", "int"), value))\n')
    main.write_text(source)
    value.write_text('formatted part means: part\nvalue means: formatted "first"\n')
    unused.write_text('to unused: @intrinsic("discard", 0)\n')
    first = build(entry=main)
    assert first[2] == b"first\n"
    assert first[0]["section_lexical_lines_reused"] == 0, first[0]
    assert first[0]["section_lexical_lines_evaluated"] > 0, first[0]
    assert first[0]["source_preparation_files_processed"] == 3, first[0]
    assert first[0]["source_preparation_files_reused"] == 0, first[0]
    value.write_text('formatted part means: part\nvalue means: formatted "second"\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"second\n"
    assert edited[0]["source_preparation_files_processed"] == 1, edited[0]
    assert edited[0]["source_preparation_files_reused"] == 2, edited[0]
    assert edited[0]["source_preparation_lines_reused"] > 0, edited[0]
    assert edited[0]["section_lexical_lines_reused"] > 0, edited[0]
    assert edited[0]["section_lexical_lines_evaluated"] > 0, edited[0]
    unused.write_text('to unused: @intrinsic("discard", 1)\n')
    formatted = build(entry=main)
    assert formatted[1:] == build(False, entry=main)[1:]
    assert formatted[2] == b"second\n"
    assert formatted[0]["source_preparation_files_processed"] == 1, formatted[0]
    assert formatted[0]["source_preparation_files_reused"] == 2, formatted[0]

    # A section-syntax edit belongs to a later producer, not file preparation.
    config = project / "config.dl"
    config.write_text('dynlex options:\n    open section: ";"\n')
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
    assert failure[0]["source_preparation_files_processed"] == 0, failure[0]
    assert failure[0]["source_preparation_files_reused"] == 3, failure[0]
    config.unlink()
    assert build(entry=main)[1:] == edited[1:]

    # A lexical input must invalidate preparation even if the resulting code agrees.
    config.write_text('dynlex options:\n    comment: "//"\n')
    lexical = build(entry=main)
    assert lexical[1:] == build(False, entry=main)[1:]
    assert lexical[0]["source_preparation_files_processed"] == 3, lexical[0]
    assert lexical[0]["source_preparation_files_reused"] == 0, lexical[0]
    config.unlink()

    # Import lookup is live even when its importing source product is reusable.
    nested = project / "nested"
    nested.mkdir()
    helper = nested / "helper.dl"
    helper.write_text("import value.dl\n")
    main.write_text(source.replace("import value.dl", "import nested/helper.dl"))
    build(entry=main)
    shadow = nested / "value.dl"
    shadow.write_text('value means: "shadow"\n')
    shadowed = build(entry=main)
    assert shadowed[2] == b"shadow\n"
    assert shadowed[1:] == build(False, entry=main)[1:]
    assert shadowed[0]["source_preparation_files_reused"] >= 2, shadowed[0]
    shadow.unlink()

    # Do not scan a later parent error ahead of an earlier imported-file error.
    main.write_text(source + '@intrinsic("discard", formatted "{unterminated")\n')
    value.write_text('@intrinsic("discard", \x07)\n')
    imported_error = build(entry=main, success=False)
    assert imported_error[1:] == build(False, entry=main, success=False)[1:]
    assert "U+0007" in imported_error[1] and "Invalid formatted string" not in imported_error[1]
    value.write_text('value means: "repaired"\n')
    parent_error = build(entry=main, success=False)
    assert parent_error[1:] == build(False, entry=main, success=False)[1:]
    assert "Invalid formatted string" in parent_error[1]
    assert parent_error[0]["source_preparation_files_reused"] > 0, parent_error[0]
    main.write_text(source)
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[2] == b"repaired\n"
    check_source_line_endings(root, build)
    check_transformed_source_chunks(root, build)


def check_source_line_endings(root, build):
    for label, ending in (("cr", b"\r"), ("crlf", b"\r\n")):
        project = root / ("prepared-" + label)
        project.mkdir()
        main, helper = project / "main.dl", project / "helper.dl"
        (project / "empty.dl").write_bytes(b"")
        source = ('to unused: @intrinsic("discard", 0)\n'
                  'import empty.dl\nimport helper.dl\n'
                  '@intrinsic("discard", @intrinsic("call", "libc", "puts", '
                  '@intrinsic("type", "int"), value))\n')
        main.write_bytes(source.encode().replace(b"\n", ending))
        helper.write_bytes(b'value means: "first"' + ending)
        build(entry=main)
        main.write_bytes((source + '@intrinsic("discard", 1)\n').encode().replace(b"\n", ending))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["source_preparation_files_processed"] == 1, edited[0]
        assert edited[0]["source_preparation_files_reused"] == 2, edited[0]
        helper.write_bytes(b'value means: "second"' + ending)
        changed = build(entry=main)
        assert changed[1:] == build(False, entry=main)[1:]
        assert changed[2] == b"second\n"


def check_transformed_source_chunks(root, build):
    project = root / "transformed-source-chunks"
    project.mkdir()
    main, helper, tail = (project / name for name in ("main.dl", "helper.dl", "tail.dl"))
    helper.write_text('to unused: @intrinsic("discard", 1)\n')
    # Import expansion places this unfinished expression before the parent's
    # import directive. Its complete expression consequently spans both files.
    tail.write_text('@intrinsic("discard",\n')
    main.write_text('import helper.dl\n'
                    'function import tail.dl value:\n    execute:\n'
                    '        @intrinsic("return", value)\n'
                    'import tail.dl\n3)\n'
                    '@intrinsic("discard", @intrinsic("call", "libc", "puts", '
                    '@intrinsic("type", "int"), "cross"))\n')
    original = build(entry=main)
    assert original[2] == b"cross\n"
    helper.write_text(helper.read_text().replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["source_transform_chunks_processed"] == 1, edited[0]
    assert edited[0]["source_transform_chunks_reused"] >= 2, edited[0]
    assert edited[0]["source_transform_input_lines_reused"] > 0, edited[0]
    assert edited[0]["section_lexical_lines_reused"] > 0, edited[0]

    # Changing a later dependency invalidates the multi-file chunk as well.
    main.write_text(main.read_text().replace('\n3)\n', '\n4)\n'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]["source_transform_chunks_processed"] > 0, changed[0]
    # Restored source slices preserve diagnostics in the parent source file.
    main.write_text(main.read_text().replace('\n4)\n', '\nmissing)\n'))
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
