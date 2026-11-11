"""An overload edit invalidates the concrete caller variants that read it."""

from test_incremental_program_cache import read_program


def check_variant_dependency_cascade(root, build):
    for indirect, separate in ((False, False), (True, False), (True, True)):
        main = root / f"variant_dependency_cascade_{indirect}_{separate}.dl"
        provider = root / "variant_dependency_provider.dl"

        def write_source(text):
            if separate:
                split = text.index('function [forward]')
                provider.write_text(text[:split])
                main.write_text('import ' + provider.name + '\n' + text[split:])
            else:
                main.write_text(text)
        source = (
            'integer means: @intrinsic("type", "int")\n'
            'decimal means: @intrinsic("type", "float", 64)\n'
            'function [selected value for] {integer:value}:\n    execute:\n'
            '        @intrinsic("return", @intrinsic("add", value, 1))\n'
            'function [selected value for] {decimal:value}:\n    execute:\n'
            '        @intrinsic("return", @intrinsic("add", value, 10.0))\n'
            'function [forward] {value}:\n    execute:\n'
            '        @intrinsic("return", selected value for value)\n'
        )
        if indirect:
            source += ('function [relay] {value}:\n    execute:\n'
                       '        @intrinsic("return", forward value)\n')
        caller = "relay" if indirect else "forward"
        for argument in ('@intrinsic("command line argument count")',
                         '@intrinsic("cast", @intrinsic("command line argument count"), decimal)'):
            source += ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                       f'"%g\\n", @intrinsic("cast", ({caller} {argument}), decimal)))\n')
        write_source(source)
        assert build(entry=main)[2] == b"1\n10\n"
        changed = source.replace('@intrinsic("add", value, 1)',
                                 '@intrinsic("add", @intrinsic("cast", value, decimal), 1.5)')
        write_source(changed)
        edited = build(entry=main)
        assert edited[2] == b"1.5\n10\n"
        assert edited[0]["preinferred_instantiations"] == 2 + int(indirect), \
            "the unaffected decimal caller variant was pre-inferred"
        assert edited[0]["native_functions_lowered"] == 3 + int(indirect), \
            "the unaffected decimal caller variant was lowered again"
        assert edited[0]["functions_ir_reused"] >= 2 + int(indirect)
        assert edited[1:] == build(False, entry=main)[1:]
        write_source(changed.replace('decimal), 1.5)', 'decimal), 2.5)'))
        stable = build(entry=main)
        assert stable[2] == b"2.5\n10\n"
        assert stable[0]["root_execution_reused"], "an unchanged variant interface did not stop the cascade"
        assert stable[0]["preinferred_instantiations"] == 1
        assert stable[0]["native_functions_lowered"] == 1
        assert stable[1:] == build(False, entry=main)[1:]

    # A caller's observed pure value depends on the nested callee's body,
    # including when the next build consumes the value from its memo table.
    main = root / "variant_nested_pure_values.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function adjust {integer:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'function relay {integer:value}:\n    execute:\n'
        '        @intrinsic("return", adjust value)\n'
        '@intrinsic("discard", relay @intrinsic("command line argument count"))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", relay 4))\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"5\n"
    changed = source.replace('"discard", 0)', '"discard", 2)')
    main.write_text(changed)
    replayed = build(entry=main)
    assert replayed[2] == b"5\n" and replayed[0]["pure_value_summary_hits"] > 0
    main.write_text(changed.replace('"add", value, 1)', '"add", value, 2)'))
    edited = build(entry=main)
    assert edited[2] == b"6\n"
    assert edited[1:] == build(False, entry=main)[1:]

    for flags in ((), ("-O2",)):
        main = root / f"variant_repeated_callback_{len(flags)}.dl"
        source = (
            'integer means: @intrinsic("type", "int")\n'
            'exposed function callback:\n    execute:\n'
            '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
            '@intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
            '@intrinsic("function", "callback")))\n'
        )
        main.write_text(source)
        assert build(entry=main, flags=flags)[2] == b"before\n"
        for message in ("after", "again"):
            main.write_text(source.replace('"before"', f'"{message}"'))
            edited = build(entry=main, flags=flags)
            assert edited[2] == (message + "\n").encode()
            assert edited[0]["root_execution_reused"]
            assert edited[1:] == build(False, entry=main, flags=flags)[1:]
            graph = read_program(root / "cache", main)["section_dependency_graph"]
            for variants in graph["inference_units"].values():
                for unit in variants.values():
                    assert unit["lookups"].keys() <= graph["callable_lookups"].keys(), \
                        "retained unit lost the candidate metadata of a lookup it consumed"
