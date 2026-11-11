"""Retain entry-point inference and IR when a body edit preserves its interface."""


def check_root_execution_reuse(root, build):
    main = root / "root_execution.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to report:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "report"))\n'
        'function show count:\n'
        '    globals:\n'
        '        count\n'
        '    execute:\n'
        '        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", count))\n'
        '@intrinsic("store", count, @intrinsic("command line argument count"))\n'
        'show count\n'
        'report\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"0\nbefore\nreport\n"

    edited = source.replace('    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n', '')
    for message in ("report", "again"):
        main.write_text(edited.replace('integer, "report"', f'integer, "{message}"'))
        rebuilt = build(entry=main)
        assert rebuilt[1:] == build(False, entry=main)[1:]
        assert rebuilt[2] == b"0\n" + message.encode() + b"\n"
        assert rebuilt[0].get("root_execution_reused"), "stable function body edit re-inferred the entry point"

    main.write_text(main.read_text().replace(
        '@intrinsic("command line argument count")',
        '@intrinsic("add", @intrinsic("command line argument count"), 1)',
    ))
    changed_root = build(entry=main)
    assert changed_root[1:] == build(False, entry=main)[1:]
    assert changed_root[2] == b"1\nagain\n"
    assert not changed_root[0]["root_execution_reused"]


def check_root_class_reuse(root, build):
    main = root / "root_class_layout.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'class:\n'
        '    patterns:\n'
        '        record\n'
        '    members:\n'
        '        number\n'
        'to report item:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        '@intrinsic("cast", item\'s number, integer)))\n'
        'to first: report @intrinsic("construct", record, 11)\n'
        'to second: report @intrinsic("construct", record, 2.5)\n'
        'to activate {integer:value}: @intrinsic("discard", value)\n'
        'activate 0\n'
        'second\n'
        'first\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"before\n2\nbefore\n11\n"
    main.write_text(source.replace('"before"', '"after"'))
    edited = build(entry=main)
    assert edited[0]["root_execution_reused"]
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"after\n2\nafter\n11\n"


def check_local_function_symbols(root, build):
    main = root / "local_symbol_names.dl"
    first = root / "local_first.dl"
    second = root / "local_second.dl"
    main.write_text('import local_first.dl\nimport local_second.dl\nfirst action\nsecond action\n')
    helper = (
        'local function helper:\n'
        '    execute:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), "{message}"))\n'
        'to {action} action: helper\n'
    )
    first.write_text(helper.format(message="first", action="first"))
    second.write_text(helper.format(message="second", action="second"))
    assert build(entry=main)[2] == b"first\nsecond\n"
    second.write_text(helper.format(message="changed", action="second"))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"first\nchanged\n"


def check_static_function_dependency(root, build):
    main = root / "static_function_dependency.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function answer:\n'
        '    execute: @intrinsic("return", 42)\n'
        'to report:\n'
        '    @intrinsic("store", callback, @intrinsic("static reference", @intrinsic("function", "answer")))\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        '@intrinsic("call pointer", @intrinsic("dereference", callback), integer)))\n'
        'report\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"42\n"
    main.write_text(source + '@intrinsic("discard", 0)\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["functions_ir_reused"] >= 1
    assert edited[0]["cached_ir_dependency_scans"] > 0
    assert edited[0]["cached_ir_dependency_hits"] > 0
    assert edited[2] == b"42\n"
