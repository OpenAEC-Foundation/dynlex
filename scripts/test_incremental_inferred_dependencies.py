"""Dependencies discovered inside instantiated section replacements."""

from test_incremental_program_cache import read_program


def check_inferred_section_dependencies(root, build):
    main = root / "inferred_section_dependencies.dl"
    print_width = (
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int"), 1, '
        '"%lld\\n", @intrinsic("size of", @intrinsic("function return type", @intrinsic("function", "callback value")))))'
    )
    source = (
        'to get callback value:\n'
        '    @intrinsic("return", 1)\n'
        'section through wrapper:\n'
        '    replacement:\n'
        f'        {print_width}\n'
        '        @intrinsic("execute body")\n'
        'to use wrapper:\n'
        '    through wrapper:\n'
        '        @intrinsic("discard", 0)\n'
        'use wrapper\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"4\n"
    program = read_program(root / "cache", main)
    declarations = program["all_function_projection"]["function_declarations"]
    callback = next(path for path, entry in declarations.items() if entry["identity"]["header"] == "callback value:")
    wrapper = program["function_sections"]["use_wrapper"]
    main.write_text(source.replace('"return", 1)', '"return", 1.0)'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:], "callback return type edit reused stale caller code"
    assert edited[2] == b"8\n"
    graph = program["section_dependency_graph"]
    assert callback in graph["forward"][wrapper], "replacement body lost its selected callback dependency"
    assert wrapper in graph["reverse"][callback]

    # An unrelated definition must not discard the saved edges of surviving sections.
    main.write_text(source + 'to unrelated: @intrinsic("discard", 0)\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"4\n"

    # A callable selected in top-level execution also owns an inferred edge.
    main.write_text(source.replace('use wrapper\n', print_width + '\n'))
    assert build(entry=main)[2] == b"4\n"
    graph = read_program(root / "cache", main)["section_dependency_graph"]
    assert callback in graph["forward"]["root"], "top-level execution lost its selected callback dependency"

    # A new typed overload can make the textual callable lookup ambiguous
    # without changing an ordinary pattern call or the existing candidate.
    main = root / "inferred_callable_candidates.dl"
    typed = (
        'integer means: @intrinsic("type", "int")\n'
        'decimal means: @intrinsic("type", "float")\n'
        + source.replace('callback value:', 'callback {integer:value}:')
        .replace('"return", 1)', '"return", value)')
        .replace('"callback value"', '"callback $"')
    )
    main.write_text(typed)
    assert build(entry=main)[2] == b"4\n"
    main.write_text(typed + 'to get callback {decimal:value}:\n    @intrinsic("return", value)\n')
    ambiguous = build(entry=main, success=False)
    assert ambiguous[1:] == build(False, entry=main, success=False)[1:]
    assert "Ambiguous function reference" in ambiguous[1]
    main.write_text(typed)
    assert build(entry=main)[2] == b"4\n"
