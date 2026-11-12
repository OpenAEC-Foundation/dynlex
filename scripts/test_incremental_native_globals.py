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
