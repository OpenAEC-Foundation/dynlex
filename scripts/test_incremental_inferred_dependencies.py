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
    wrapper = next(path for path, entry in declarations.items() if entry["identity"]["header"] == "use wrapper:")
    assert wrapper in program["function_sections"].values()
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


def check_callable_trial_dependencies(root, build):
    main = root / "inferred_callable_trial.dl"
    hidden = root / "inferred_callable_hidden.dl"
    hidden.write_text("\n")
    source = (
        'import inferred_callable_hidden.dl\n'
        'integer means: @intrinsic("type", "int")\n'
        'decimal means: @intrinsic("type", "float")\n'
        'class:\n'
        '    patterns:\n'
        '        box\n'
        '    members:\n'
        '        value as integer\n'
        'convert item:\n'
        '    replacement:\n'
        '        @intrinsic("construct", box, 1)\n'
        'convert {integer:item}:\n'
        '    replacement:\n'
        '        @intrinsic("discard", @intrinsic("function", "special callback $"))\n'
        '        @intrinsic("construct", box, 2)\n'
        'to report:\n'
        '    @intrinsic("store", result, @intrinsic("cast", 7, box))\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", result\'s value))\n'
        'report\n'
    )
    main.write_text(source)
    missing = build(entry=main)
    assert missing[2] == b"1\n"
    assert missing[0]["failed_callable_lookups_observed"] > 0
    source += '@intrinsic("discard", 0)\n'
    main.write_text(source)
    reused = build(entry=main)
    assert reused[2] == b"1\n" and reused[0]["functions_ir_reused"] >= 1

    # A candidate in another file's local scope does not change this lookup.
    hidden.write_text('local function special callback {decimal:value}:\n    execute:\n        @intrinsic("discard", value)\n')
    invisible = build(entry=main)
    assert invisible[2] == b"1\n" and invisible[0]["functions_ir_reused"] >= 1
    assert invisible[0]["callable_lookups_checked"] > 0 and invisible[0]["callable_lookups_changed"] == 0

    # Adding a visible candidate changes the outcome of the rejected trial.
    main.write_text(source + 'to special callback {integer:value}: @intrinsic("discard", value)\n')
    available = build(entry=main)
    assert available[1:] == build(False, entry=main)[1:], "a cached failed lookup kept the more specific conversion ineligible"
    assert available[2] == b"2\n"
    assert available[0]["callable_lookups_changed"] == 1
    hidden.write_text("\n")
    still_available = build(entry=main)
    assert still_available[2] == b"2\n" and still_available[0]["functions_ir_reused"] >= 1
    assert still_available[0]["callable_lookups_checked"] > 0 and still_available[0]["callable_lookups_changed"] == 0
    main.write_text(source)
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert removed[2] == b"1\n"

    # A conversion that produced the wrong type also remains a dependency.
    main = root / "inferred_conversion_trial.dl"
    conversion = source.replace('import inferred_callable_hidden.dl\n', '')
    conversion = conversion.replace(
        '        @intrinsic("discard", @intrinsic("function", "special callback $"))\n'
        '        @intrinsic("construct", box, 2)\n',
        '        0\n',
    )
    main.write_text(conversion)
    assert build(entry=main)[2] == b"1\n"
    main.write_text(conversion.replace('        0\n', '        @intrinsic("construct", box, 2)\n'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:], "rejected conversion body edit reused stale caller code"
    assert changed[2] == b"2\n"

    # A callable body can be read by a failed trial without leaving an
    # instantiation. Fixing the body must still invalidate that trial.
    main = root / "inferred_callable_trial_body.dl"
    callback = source.replace('import inferred_callable_hidden.dl\n', '') + (
        'to special callback {integer:value}: @intrinsic("discard", value)\n'
    )
    main.write_text(callback.replace(
        'callback {integer:value}: @intrinsic("discard", value)',
        'callback {integer:value}: @intrinsic("add", value, "x")',
    ))
    assert build(entry=main)[2] == b"1\n"
    main.write_text(callback)
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:], "a failed callable trial was masked as an inactive body"
    assert changed[2] == b"2\n"

    # A function consumed by a restored type constraint remains active even
    # when it has neither a current instantiation nor generated LLVM code.
    main = root / "inferred_callable_constraint.dl"
    constraint = (
        'to get callback result:\n'
        '    @intrinsic("return", 1)\n'
        'callback type means: @intrinsic("function return type", @intrinsic("function", "callback result"))\n'
        'to accept {callback type:value}: @intrinsic("discard", value)\n'
        'accept 1\n'
    )
    main.write_text(constraint)
    build(entry=main)
    constraint += '@intrinsic("discard", 0)\n'
    main.write_text(constraint)
    restored = build(entry=main)
    assert restored[0]["constraint_values_reused"] >= 1
    cached = read_program(root / "cache", main)
    keys = cached["all_function_projection"]["constraint_probe_schedule"]["callable_lookups"]
    assert any(keys)
    main.write_text(constraint.replace('"return", 1)', '"return", 1.0)'))
    changed = build(entry=main, success=False)
    assert changed[1:] == build(False, entry=main, success=False)[1:], "a restored constraint lost its callable dependency"
