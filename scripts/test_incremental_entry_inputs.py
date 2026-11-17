"""Entry replay consumes every constraint result and compatible class prefix."""

from test_incremental_program_cache import read_program


def check_entry_input_products(root, build):
    check_number_rule_result(root, build)
    check_partial_class_result(root, build)


def check_number_rule_result(root, build):
    main = root / "entry_number_rule.dl"
    source = (
        'numbers are properties means: @intrinsic("number rule", properties)\n'
        'rule properties means: @intrinsic("type", "int", 64)\n'
        'in this file:\n'
        '    numbers are rule properties\n'
        'to report:\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", '
        '@intrinsic("type", "int", 32), "before"))\n'
        'report\n'
    )
    main.write_text(source)
    build(entry=main)
    for message in ("after", "again"):
        main.write_text(source.replace('"before"', f'"{message}"'))
        changed = build(entry=main)
        assert changed[1:] == build(False, entry=main)[1:]
        assert changed[2] == message.encode() + b"\n"
        assert changed[0]["root_execution_reused"], "number rule prevented entry replay"
        assert changed[0]["constraint_values_reused"] == 1
        schedule = read_program(root / "cache", main)["all_function_projection"]["constraint_probe_schedule"]
        assert len(schedule["values"]) == 1 and schedule["values"][0] is not None

    # A changed provider must recompute its result before validating entry inputs.
    changed_rule = source.replace('"int", 64', '"int", 32')
    main.write_text(changed_rule)
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert not changed[0]["root_execution_reused"]
    main.write_text(changed_rule.replace('"before"', '"updated"'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]["root_execution_reused"]

    # A rejected provider result must not leave a reusable successful rule behind.
    main.write_text(source.replace('@intrinsic("type", "int", 64)', '0'))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    warning = source.replace('@intrinsic("type", "int", 64)', '@intrinsic("type", "any")')
    main.write_text(warning)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(warning.replace('"before"', '"warning retained"'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]["constraint_values_reused"] == 1


def check_partial_class_result(root, build):
    main = root / "entry_class_prefix.dl"
    source = (
        'class:\n    patterns:\n        record\n    members:\n        number\n'
        '@intrinsic("store", shared, @intrinsic("construct", record, '
        '@intrinsic("command line argument count")))\n'
        'function show:\n    globals: shared\n    execute:\n'
        '        @intrinsic("variadic call", "libc", "printf", '
        '@intrinsic("type", "nothing"), 1, "%d\\n", @intrinsic("add", shared\'s number, 1))\n'
        'show\n'
        '@intrinsic("discard", @intrinsic("construct", record, 1.5))\n'
    )
    main.write_text(source)
    build(entry=main)
    for value in (2, 3):
        main.write_text(source.replace("number, 1)", f"number, {value})"))
        changed = build(entry=main)
        assert changed[1:] == build(False, entry=main)[1:]
        assert changed[2] == f"{value}\n".encode()
        assert changed[0]["root_execution_reused"], "compatible class prefix prevented entry replay"
        assert changed[0]["global_type_producers_restored"] == 1

    main.write_text(source.replace('record, 1.5', 'record, "different layout"'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert not changed[0]["root_execution_reused"]
