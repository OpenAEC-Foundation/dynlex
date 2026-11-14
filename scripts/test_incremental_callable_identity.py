"""Reused and rebuilt code must share a callable's function address."""


def check_callable_identity_reuse(root, build):
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'pointer means: @intrinsic("add pointer depth", @intrinsic("type", "int", 8))\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
        'function [callback A]:\n    execute: @intrinsic("return", 1)\n'
        'function [callback B]:\n    execute: @intrinsic("return", 2)\n'
        'function [choose] {value}:\n    execute:\n'
        '        if @intrinsic("equal", value, 4):\n'
        '            @intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback A"), pointer))\n'
        '        @intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback B"), pointer))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        '@intrinsic("cast", @intrinsic("equal", choose @intrinsic("command line argument count"), '
        '@intrinsic("cast", @intrinsic("function", "callback A"), pointer)), integer)))\n'
    )
    old = '        @intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback B"), pointer))'
    for flags in ((), ("-O2",)):
        main = root / f"callable_identity_{len(flags)}.dl"
        main.write_text(source)
        assert build(entry=main, flags=flags)[2] == b"0\n"
        main.write_text(source.replace(old, old.replace('callback B', 'callback A')))
        changed = build(entry=main, flags=flags)
        assert changed[2] == b"1\n", "reused and rebuilt code observed different wrappers for the same function"
        assert changed[0]["root_execution_reused"]
        assert changed[1:] == build(False, entry=main, flags=flags)[1:]
        main.write_text(main.read_text().replace('"return", 1)', '"return", 3)'))
        callback = build(entry=main, flags=flags)
        assert callback[2] == b"1\n" and callback[0]["root_execution_reused"]
        assert callback[1:] == build(False, entry=main, flags=flags)[1:]
