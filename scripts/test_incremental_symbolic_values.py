"""Symbolic function results retain only their actual compile-time consumers."""


def check_symbolic_value_queries(root, build):
    def output(value):
        return ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
                '@intrinsic("cast", @intrinsic("equal", ' + value + ', '
                '@intrinsic("cast", @intrinsic("function", "callback A"), pointer)), integer)))\n')

    first = '            @intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback A"), pointer))'
    second = '        @intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback B"), pointer))'
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'pointer means: @intrinsic("add pointer depth", @intrinsic("type", "int", 8))\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
        'function [callback A]:\n    execute: @intrinsic("return", 1)\n'
        'function [callback B]:\n    execute: @intrinsic("return", 2)\n'
        'function [choose] {value}:\n    execute:\n'
        '        if @intrinsic("equal", value, 4):\n' + first + '\n' + second + '\n'
        'to report four:\n    ' + output('choose 4') +
        'to report five:\n    ' + output('choose 5') +
        output('choose @intrinsic("command line argument count")') + 'report four\nreport five\n'
    )
    for flags in ((), ("-O2",)):
        main = root / f"symbolic_value_queries_{len(flags)}.dl"
        main.write_text(source)
        assert build(entry=main, flags=flags)[2] == b"0\n1\n0\n"
        changed = source.replace(second, second.replace('callback B', 'callback A'))
        for text, expected in ((changed, b"1\n1\n1\n"),
                               (changed.replace(first, first.replace('callback A', 'callback B')), b"1\n0\n1\n")):
            main.write_text(text)
            edited = build(entry=main, flags=flags)
            assert edited[2] == expected and edited[1:] == build(False, entry=main, flags=flags)[1:]
            assert edited[0]["root_execution_reused"]
            assert edited[0]["preinferred_instantiations"] == 2, \
                "an unchanged symbolic result invalidated its consumer"
            assert edited[0]["value_dependency_queries_reused"] > 0
    check_cyclic_symbolic_values(root, build)
    check_symbolic_argument_keys(root, build)


def check_cyclic_symbolic_values(root, build):
    main = root / "cyclic_symbolic_values.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'pointer means: @intrinsic("add pointer depth", @intrinsic("type", "int", 8))\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
    )
    for name, other in (("first", "second"), ("second", "first")):
        source += (
            f'function [{name}] {{integer:value}}:\n    execute:\n'
            '        if @intrinsic("equal", value, 0):\n'
            '            @intrinsic("return", @intrinsic("construct", pointer))\n'
            '        @intrinsic("return", @intrinsic("cast", '
            f'@intrinsic("function", "{other} $"), pointer))\n'
        )
    for name, other in (("first", "second"), ("second", "first")):
        source += (
            '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
            f'@intrinsic("cast", @intrinsic("equal", {name} 1, '
            f'@intrinsic("cast", @intrinsic("function", "{other} $"), pointer)), integer)))\n'
        )
    source += '@intrinsic("discard", 10)\n'
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n1\n"
    for value in (11, 12):
        main.write_text(source.replace('"discard", 10)', f'"discard", {value})'))
        edited = build(entry=main)
        assert edited[2] == b"1\n1\n" and edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["pure_value_summary_hits"] >= 2
        assert edited[0]["function_body_inference_passes"] == 0, \
            "restoring cyclic symbolic values reran unchanged function inference"


def check_symbolic_argument_keys(root, build):
    main = root / "symbolic_argument_keys.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'pointer means: @intrinsic("add pointer depth", @intrinsic("type", "int", 8))\n'
        'function [callback]:\n    execute: @intrinsic("return", 1)\n'
        'function [identity] {@intrinsic("fix", pointer):action}:\n    execute:\n'
        '        @intrinsic("return", action)\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        '@intrinsic("cast", @intrinsic("equal", identity @intrinsic("cast", @intrinsic("function", "callback"), pointer), '
        '@intrinsic("cast", @intrinsic("function", "callback"), pointer)), integer)))\n'
        '@intrinsic("discard", 10)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n"
    changed = source.replace('"discard", 10)', '"discard", 11)')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[2] == b"1\n" and edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["pure_value_summary_hits"] > 0
    assert edited[0]["function_body_inference_passes"] == 0, "a fixed symbolic argument lost its inference key"
    changed = changed.replace('        @intrinsic("return", action)',
                              '        @intrinsic("discard", 5)\n        @intrinsic("return", action)')
    main.write_text(changed)
    identity = build(entry=main)
    assert identity[2] == b"1\n" and identity[1:] == build(False, entry=main)[1:]
    assert identity[0]["root_execution_reused"]
    assert identity[0]["function_body_inference_passes"] == 1
    main.write_text(changed.replace('"return", 1)', '"return", 2)'))
    callback = build(entry=main)
    assert callback[2] == b"1\n" and callback[1:] == build(False, entry=main)[1:]
    assert callback[0]["root_execution_reused"]
    assert callback[0]["function_body_inference_passes"] == 1
    changed = changed.replace('"return", 1)', '"return", "changed ABI")')
    main.write_text(changed)
    signature = build(entry=main)
    assert signature[2] == b"1\n" and signature[1:] == build(False, entry=main)[1:]
    main.write_text(changed.replace('function [callback]:\n    execute: @intrinsic("return", "changed ABI")\n', ''))
    missing = build(entry=main, success=False)
    assert missing[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(changed)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
