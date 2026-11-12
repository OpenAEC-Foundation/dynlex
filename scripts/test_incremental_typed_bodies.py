"""Unchanged inferred bodies execute new inputs across compiler processes."""


def check_typed_body_reuse(root, build):
    main = root / "typed_body_new_arguments.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [advance] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", advance 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n"
    for value in (2, 3):
        main.write_text(source.replace('advance 1)', f'advance {value})'))
        edited = build(entry=main)
        assert edited[2] == f"{value + 1}\n".encode()
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["function_body_inference_passes"] == 0, \
            "an unseen argument re-inferred an unchanged typed body"
        assert edited[0]["inference_bodies_loaded"] > 0
    main.write_text(source.replace('advance 1)', 'advance @intrinsic("command line argument count"))'))
    runtime = build(entry=main)
    assert runtime[2] == b"1\n" and runtime[1:] == build(False, entry=main)[1:]
    assert runtime[0]["function_body_inference_passes"] == 0, "a runtime call re-inferred a cached compile-time body"
    assert runtime[0]["inference_bodies_loaded"] > 0
    check_nested_typed_bodies(root, build)
    check_flex_typed_body(root, build)
    check_class_typed_body(root, build)
    check_pack_typed_body(root, build)
    check_typed_body_eviction(root, build)
    check_typed_body_scope_scaling(root, build)
    check_recursive_class_requests(root, build)
    check_impure_callback_reuses_ir(root, build)
    check_impure_signature_promotion(root, build)


def check_nested_typed_bodies(root, build):
    main = root / "typed_body_nested_calls.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [advance] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'function [twice] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", advance (advance value))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", twice 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"3\n"
    for value in (2, 3):
        main.write_text(source.replace('twice 1)', f'twice {value})'))
        changed = build(entry=main)
        assert changed[2] == f"{value + 2}\n".encode() and changed[1:] == build(False, entry=main)[1:]
        assert changed[0]["function_body_inference_passes"] == 0
        assert changed[0]["inference_bodies_loaded"] == 2


def check_flex_typed_body(root, build):
    main = root / "typed_body_captured_locals.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'flex section add private seed to target:\n    replacement:\n'
        '        @intrinsic("store", saved, seed)\n'
        '        @intrinsic("execute body")\n'
        '        @intrinsic("store", target, @intrinsic("add", target, saved))\n'
        'function [calculate] {integer:seed}:\n    execute:\n'
        '        @intrinsic("store", total, 0)\n'
        '        add private seed to total:\n'
        '            add private saved to total:\n'
        '                @intrinsic("discard", 0)\n'
        '        @intrinsic("return", total)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", calculate 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n"
    for value in (2, 3):
        # Move source lines too: a cached body must bind to this process's
        # declarations and templates, rather than replay physical positions.
        main.write_text(f'# moved body {value}\n' + source.replace('calculate 1)', f'calculate {value})'))
        changed = build(entry=main)
        assert changed[2] == f"{value * 2}\n".encode() and changed[1:] == build(False, entry=main)[1:]
        assert changed[0]["function_body_inference_passes"] == 0
        assert changed[0]["inference_bodies_loaded"] > 0


def check_class_typed_body(root, build):
    main = root / "typed_body_local_class.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'class:\n    patterns:\n        row\n    members:\n        value\n'
        'class:\n    patterns:\n        other row\n    members:\n        other value\n'
        'function [calculate] {input}:\n    execute:\n'
        '        @intrinsic("store", result, @intrinsic("construct", row, input))\n'
        '        @intrinsic("return", result\'s value)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", calculate 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n"
    for value in (2, 3):
        main.write_text(source.replace('calculate 1)', f'calculate {value})'))
        changed = build(entry=main)
        assert changed[2] == f"{value}\n".encode() and changed[1:] == build(False, entry=main)[1:]
        assert changed[0]["function_body_inference_passes"] == 0
        assert changed[0]["inference_bodies_loaded"] > 0
        assert changed[0]["inference_classes_loaded"] == 1
        assert changed[0]["inference_classes_restored"] == 1


def check_pack_typed_body(root, build):
    main = root / "typed_body_argument_captures.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'flex section while condition:\n    replacement: @intrinsic("loop while", condition)\n'
        'flex section backward item in values:\n    replacement:\n'
        '        @intrinsic("store", position, @intrinsic("subtract", @intrinsic("type extent", values, 0), 1))\n'
        '        while @intrinsic("greater than or equal", position, 0):\n'
        '            @intrinsic("store", item, @intrinsic("extract element", values, position))\n'
        '            @intrinsic("execute body")\n'
        '            @intrinsic("store", position, @intrinsic("subtract", position, 1))\n'
        'function decimal backward {arguments:digits}:\n    replacement:\n'
        '        @intrinsic("store", result, 0)\n'
        '        backward digit in digits:\n'
        '            @intrinsic("store", result, @intrinsic("add", @intrinsic("multiply", result, 10), digit))\n'
        '        {result}\n'
        'function [calculate] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", (decimal backward value, 2 and 3))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", calculate 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"321\n"
    for value in (2, 3):
        main.write_text(source.replace('calculate 1)', f'calculate {value})'))
        changed = build(entry=main)
        assert changed[2] == f"{320 + value}\n".encode() and changed[1:] == build(False, entry=main)[1:]
        assert changed[0]["function_body_inference_passes"] == 0
        assert changed[0]["inference_bodies_loaded"] > 0


def check_typed_body_eviction(root, build):
    from test_incremental_summary_units import summary_manifest

    main = root / "typed_body_eviction.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [advance] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", advance 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n"
    manifests = summary_manifest(root, main)
    reference = next(reference for variants in manifests.values() for reference in variants.values())
    body = root / "cache" / "native-v1" / f'inference-body-{reference["body"]["payload"]}.entry'
    body.unlink()
    main.write_text(source.replace('advance 1)', 'advance 2)'))
    missing = build(entry=main)
    assert missing[2] == b"3\n" and missing[1:] == build(False, entry=main)[1:]
    assert missing[0]["function_body_inference_passes"] == 1
    main.write_text(source.replace('advance 1)', 'advance 3)'))
    recovered = build(entry=main)
    assert recovered[2] == b"4\n" and recovered[1:] == build(False, entry=main)[1:]
    assert recovered[0]["function_body_inference_passes"] == 0


def check_typed_body_scope_scaling(root, build):
    from test_incremental_summary_units import summary_manifest

    main = root / "typed_body_scope_scaling.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'class:\n    patterns:\n        record\n    members:\n'
        + ''.join(f'        field {index} as integer\n' for index in range(64))
        + 'flex section block:\n    replacement: @intrinsic("execute body")\n'
        'function [calculate] {record:row} {integer:seed}:\n    execute:\n'
    )
    for _ in range(12):
        for depth in range(12):
            source += ' ' * (8 + depth * 4) + 'block:\n'
        source += ' ' * 56 + '@intrinsic("discard", seed)\n'
    source += (
        '        @intrinsic("return", @intrinsic("add", row\'s field 0, seed))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        'calculate @intrinsic("construct", record, ' + ', '.join('1' for _ in range(64)) + ') 1))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n"
    references = summary_manifest(root, main)
    payloads = [root / "cache" / "native-v1" / f'inference-body-{reference["body"]["payload"]}.entry'
                for variants in references.values() for reference in variants.values() if "body" in reference]
    # A short body with nested sections must not repeat its large argument layout
    # throughout every scope ancestry. Leave room for its actual typed expressions.
    assert sum(path.stat().st_size for path in payloads) < 1536 * 1024
    main.write_text(source.replace(') 1))', ') 2))'))
    changed = build(entry=main)
    assert changed[2] == b"3\n" and changed[1:] == build(False, entry=main)[1:]
    assert changed[0]["function_body_inference_passes"] == 0


def check_recursive_class_requests(root, build):
    from test_incremental_summary_units import summary_manifest
    from test_incremental_program_cache import read_program

    source = (
        'integer means: @intrinsic("type", "int")\n'
        'a pointer to pointee means: @intrinsic("add pointer depth", pointee)\n'
        'class:\n    patterns:\n        record\n    members:\n        next as a pointer to other record\n        value as integer\n'
        'class:\n    patterns:\n        other record\n    members:\n        next as a pointer to record\n'
        'pointer means: @intrinsic("add pointer depth", record)\n'
        'function [offset]:\n    execute: @intrinsic("return", 1)\n'
        'function [change] {integer:value} [in] {pointer:target}:\n    execute:\n'
        '        @intrinsic("store at", target, @intrinsic("construct", record, @intrinsic("cast", 0, @intrinsic("add pointer depth", other record)), @intrinsic("add", value, offset)))\n'
        'function [wrapper] {pointer:target}:\n    execute: change @intrinsic("command line argument count") in target\n'
        'exposed function [kept] {pointer:target}:\n    execute: wrapper target\n'
        '@intrinsic("store", box, @intrinsic("construct", record, @intrinsic("cast", 0, @intrinsic("add pointer depth", other record)), 0))\n'
        'wrapper @intrinsic("address of", box)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", box\'s value))\n'
        '@intrinsic("discard", 10)\n'
    )
    main = root / 'typed_body_recursive_class_requests.dl'
    main.write_text(source)
    assert build(entry=main)[2] == b'1\n'
    for value in (11, 12, 13):
        program = read_program(root / 'cache', main)
        manifests = summary_manifest(root, main)
        for owner, declaration in program['all_function_projection']['function_declarations'].items():
            if declaration['identity']['header'].startswith('function [wrapper]'):
                for reference in manifests[owner].values():
                    assert reference['body'] is None, "an impure function stored an unused executable body"
                    assert reference['classes'], "native inference facts lost their class-layout units"
        path = next(path for path, decl in program['all_function_projection']['function_declarations'].items()
                    if decl['identity']['header'].startswith('function [offset]'))
        for reference in summary_manifest(root, main)[path].values():
            (root / 'cache/native-v1' / f'inference-summary-{reference["payload"]}.entry').unlink()
        main.write_text(source.replace('"discard", 10)', f'"discard", {value})'))
        edited = build(entry=main)
        assert edited[2] == b'1\n' and edited[1:] == build(False, entry=main)[1:]
        assert edited[0]['functions_ir_reused'] > 0

    # Keep an independent constraint reusable while a field-type provider in
    # the recursive cycle changes. Equal resulting layouts still need one
    # canonical cycle, rather than a restored cycle plus a duplicate.
    source = (
        'floating means: @intrinsic("type", "float")\n'
        'function [observe] {floating:value}:\n    execute: @intrinsic("discard", value)\n'
        + source.replace('integer means: @intrinsic("type", "int")\n',
                         'function [integer]:\n    execute: @intrinsic("return", @intrinsic("type", "int"))\n')
        + 'observe 1.0\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b'1\n'
    main.write_text(source.replace('"type", "int")', '"type", "int", 32)'))
    changed = build(entry=main)
    assert changed[2] == b'1\n' and changed[1:] == build(False, entry=main)[1:]
    assert changed[0]['constraint_values_reused'] > 0


def check_impure_callback_reuses_ir(root, build):
    from test_incremental_program_cache import read_program
    from test_incremental_summary_units import summary_manifest

    main = root / 'typed_body_impure_callback_promotion.dl'
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [handler]:\n    execute:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "handler"))\n'
        'function [inspect] {callback}:\n    execute: @intrinsic("return", 1)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        'inspect @intrinsic("function", "handler")))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b'1\n'
    program = read_program(root / 'cache', main)
    owner = next(path for path, declaration in program['all_function_projection']['function_declarations'].items()
                 if declaration['identity']['header'].startswith('function [handler]'))
    assert owner in program['function_sections'].values()
    assert all(reference['body'] is None for reference in summary_manifest(root, main)[owner].values())
    main.write_text(source + 'handler\n')
    promoted = build(entry=main)
    assert promoted[2] == b'1\nhandler\n' and promoted[1:] == build(False, entry=main)[1:]
    assert promoted[0]['function_body_inference_passes'] == 0
    assert promoted[0]['functions_ir_reused'] > 0


def check_impure_signature_promotion(root, build):
    from test_incremental_program_cache import read_program
    from test_incremental_summary_units import summary_manifest

    main = root / 'typed_body_signature_promotion.dl'
    definition = (
        'integer means: @intrinsic("type", "int")\n'
        'function [handler]:\n    execute:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "handler"))\n'
    )
    source = definition + '@intrinsic("discard", @intrinsic("type of", handler))\n'
    main.write_text(source)
    assert build(entry=main)[2] == b''
    program = read_program(root / 'cache', main)
    owner = next(path for path, declaration in program['all_function_projection']['function_declarations'].items()
                 if declaration['identity']['header'].startswith('function [handler]'))
    assert owner not in program['function_sections'].values()
    assert all(reference['body'] for reference in summary_manifest(root, main)[owner].values())
    main.write_text(definition + '@intrinsic("discard", 0)\n')
    assert build(entry=main)[2] == b''
    main.write_text(definition + 'handler\n')
    promoted = build(entry=main)
    assert promoted[2] == b'handler\n' and promoted[1:] == build(False, entry=main)[1:]
    assert promoted[0]['function_body_inference_passes'] == 0
    assert promoted[0]['inference_bodies_loaded'] > 0
    assert all(reference['body'] is None for reference in summary_manifest(root, main)[owner].values())
