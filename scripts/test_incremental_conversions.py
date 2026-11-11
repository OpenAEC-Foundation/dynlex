"""Reuse conversion bodies and invalidate their successful and rejected callers."""


def check_conversion_reuse(root, build):
    for modifier in ("", "implicitly "):
        name = "implicit" if modifier else "explicit"
        main = root / f"incremental_{name}_conversion.dl"
        source = (
            'integer means: @intrinsic("type", "int")\n'
            'class:\n'
            '    patterns:\n'
            '        box\n'
            '    members:\n'
            '        number as integer\n'
            f'{modifier}convert {{integer:value}}:\n'
            '    execute: @intrinsic("return", @intrinsic("construct", box, @intrinsic("add", value, 11)))\n'
            'function report {box:value}:\n'
            '    execute: @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
            '"%d\\n", value\'s number))\n'
            'function run {integer:number}:\n'
            '    execute: report ' + ('number' if modifier else '@intrinsic("cast", number, box)') + '\n'
            'run @intrinsic("command line argument count")\n'
        )
        main.write_text(source)
        assert build(entry=main)[2] == b"11\n"
        main.write_text(source + '@intrinsic("discard", 0)\n')
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["functions_ir_reused"] >= 3, "conversion dependency prevented caller IR reuse"
        main.write_text((source + '@intrinsic("discard", 0)\n').replace('"add", value, 11', '"add", value, 12'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == b"12\n"
        assert edited[0]["root_execution_reused"]
        assert edited[0]["preinferred_instantiations"] >= 1

        # A rejected conversion still contributes to overload selection.
        alternative = (
            'class:\n'
            '    patterns:\n'
            '        spare\n'
            '    members:\n'
            '        number as integer\n'
            f'{modifier}convert {{integer:other}}:\n'
            '    execute: @intrinsic("return", @intrinsic("construct", spare, other))\n'
        )
        main.write_text(source + alternative)
        assert build(entry=main)[2] == b"11\n"
        main.write_text(source + alternative.replace('@intrinsic("construct", spare, other)', '@intrinsic("construct", box, other)'))
        ambiguous = build(success=False, entry=main)
        assert ambiguous[1:] == build(False, success=False, entry=main)[1:]
        assert "equally specific" in ambiguous[1]
        main.write_text(source)
        assert build(entry=main)[2] == b"11\n"
        main.write_text(source + alternative.replace('@intrinsic("construct", spare, other)', '@intrinsic("construct", box, other)'))
        added = build(success=False, entry=main)
        assert added[1:] == build(False, success=False, entry=main)[1:]
        assert "equally specific" in added[1]

