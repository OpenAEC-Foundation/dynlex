def check_native_global_reuse(root, build):
    expressions = (
        ('@intrinsic("command line argument count")', b"0\n"),
        ('@intrinsic("cast", @intrinsic("equal", '
         '@intrinsic("dereference", @intrinsic("command line argument values")), '
         '@intrinsic("cast", 0, @intrinsic("add pointer depth", @intrinsic("type", "int", 8)))), integer)',
         b"1\n"),
    )
    for flags in ((), ("-O2",)):
        for index, (expression, output) in enumerate(expressions):
            main = root / f"native_argument_globals_{len(flags)}_{index}.dl"
            source = f'''integer means: @intrinsic("type", "int")
function [read argument state]:
    execute: @intrinsic("return", {expression})
function [report arguments]:
    execute:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", read argument state))
report arguments
@intrinsic("discard", 10)
'''
            main.write_text(source)
            assert build(entry=main, flags=flags)[2] == output
            for value in (11, 12):
                main.write_text(source.replace('"discard", 10', f'"discard", {value}'))
                edited = build(entry=main, flags=flags)
                clean = build(False, entry=main, flags=flags)
                assert edited[1:] == clean[1:]
                assert not edited[0]["root_execution_reused"]
                assert edited[0]["function_body_inference_passes"] == 0, \
                    "an unchanged function reading native argument state was inferred again"
                assert edited[0]["inference_bodies_loaded"] == 0
                assert edited[0]["functions_ir_reused"] == 2
    check_transitive_global_reuse(root, build)
    check_flex_global_reuse(root, build)
    check_callback_global_reuse(root, build)


def check_transitive_global_reuse(root, build):
    for flags in ((), ("-O2",)):
        for shadow in (False, True):
            main = root / f"transitive_global_reader_{len(flags)}_{int(shadow)}.dl"
            parameter = ' {integer:observed}' if shadow else ''
            call_argument = ' 5' if shadow else ''
            value = '@intrinsic("cast", read state, integer)'
            if shadow:
                value = f'@intrinsic("add", {value}, observed)'
            source = f'''integer means: @intrinsic("type", "int")
@intrinsic("store", observed, 7)
function [read state]:
    globals: observed
    execute: @intrinsic("return", observed)
function [report state]{parameter}:
    execute: @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", {value}))
report state{call_argument}
@intrinsic("discard", 10)
'''
            main.write_text(source)
            assert build(entry=main, flags=flags)[2] == (b"12\n" if shadow else b"7\n")
            for old, new in (('"discard", 10', '"discard", 11'), ('observed, 7', 'observed, 8')):
                source = source.replace(old, new)
                main.write_text(source)
                edited = build(entry=main, flags=flags)
                assert edited[1:] == build(False, entry=main, flags=flags)[1:]
                assert edited[0]["function_body_inference_passes"] == 0, \
                    "a caller lost IR reuse because only its callee's globals were recorded"
            main.write_text(source.replace('observed, 8)', 'observed, 8.0)'))
            typed = build(entry=main, flags=flags)
            assert typed[1:] == build(False, entry=main, flags=flags)[1:]
            assert typed[0]["function_body_inference_passes"] > 0, "a changed global type reused stale facts"


def check_flex_global_reuse(root, build):
    main = root / "flex_global_reader.dl"
    source = '''integer means: @intrinsic("type", "int")
emit value means: @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", value)
@intrinsic("store", observed, 7)
function [read state]:
    globals: observed
    execute: @intrinsic("return", observed)
function [report state]:
    execute: @intrinsic("discard", emit read state)
report state
@intrinsic("discard", 10)
'''
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    main.write_text(source.replace('"discard", 10', '"discard", 11'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["function_body_inference_passes"] == 0, \
        "typing a flex argument discarded a global type dependency"


def check_callback_global_reuse(root, build):
    template = '''integer means: @intrinsic("type", "int")
@intrinsic("store", observed, 7)
function [report state]:
    globals: observed
    execute: @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", @intrinsic("cast", observed, integer)))
function [register handler]:
    execute: @intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, @intrinsic("function", "report state")))
register handler
@intrinsic("discard", 10)
'''
    for flags in ((), ("-O2",)):
        source = template
        main = root / f"callback_global_reader_{len(flags)}.dl"
        main.write_text(source)
        assert build(entry=main, flags=flags)[2] == b"7\n"
        for old, new, output in (('"discard", 10', '"discard", 11', b"7\n"),
                                 ('observed, 7', 'observed, 8', b"8\n")):
            source = source.replace(old, new)
            main.write_text(source)
            edited = build(entry=main, flags=flags)
            assert edited[2] == output
            assert edited[1:] == build(False, entry=main, flags=flags)[1:]
            assert edited[0]["function_body_inference_passes"] == 0, \
                "a callback's global input blocked reuse of its registration code"
            assert edited[0]["cached_ir_global_input_hits"] > 0
        main.write_text(source.replace('observed, 8)', 'observed, 8.0)'))
        typed = build(entry=main, flags=flags)
        assert typed[2] == b"8\n"
        assert typed[1:] == build(False, entry=main, flags=flags)[1:]
        assert typed[0]["function_body_inference_passes"] > 0
