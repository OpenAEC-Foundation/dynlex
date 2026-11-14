"""Concrete inference units depend on the global types they actually access."""

import json
from test_incremental_program_cache import read_program
from test_incremental_global_callers import check_global_caller_interfaces


def check_global_type_inputs(root, build):
    main = root / "global_type_inputs.dl"
    source = ('@intrinsic("store", kept, 7)\n'
              '@intrinsic("store", unrelated, 2)\n'
              'function report:\n    globals: kept\n    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", kept, @intrinsic("type", "int", 64)))\n'
              'function other:\n    globals: unrelated\n    execute: @intrinsic("discard", unrelated)\n'
              'report\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"7\n"
    main.write_text(source.replace('unrelated, 2', 'unrelated, 2.5'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"7\n"
    assert edited[0]["function_body_inference_passes"] == 0, \
        "an unrelated global type invalidated a function that only reads kept"
    assert edited[0]["source_function_body_clones"] == 0
    assert edited[0]["inference_instantiations_reused"] >= 1
    program = read_program(root / "cache", main)
    for variants in program["inference_summaries"].values():
        for summary in variants.values():
            assert all(json.loads(key)[1] == "kept" for key in summary["global_inputs"])

    changed = source.replace('unrelated, 2', 'unrelated, 2.5').replace('kept, 7', 'kept, 2147483648')
    main.write_text(changed)
    relevant = build(entry=main)
    assert relevant[1:] == build(False, entry=main)[1:]
    assert relevant[2] == b"2147483648\n"
    assert relevant[0]["function_body_inference_passes"] == 1
    main.write_text(changed.replace('unrelated, 2.5', 'unrelated, 2'))
    again = build(entry=main)
    assert again[1:] == build(False, entry=main)[1:]
    assert again[0]["function_body_inference_passes"] == 0

    # An untyped, unused global must not prevent caching a different reader.
    main = root / "global_unknown_input.dl"
    unknown = (source.replace('@intrinsic("store", unrelated, 2)\n', '')
               .replace('"discard", unrelated', '"discard", 0')
               + 'section parked:\n    replacement: @intrinsic("discard", unrelated)\n')
    main.write_text(unknown)
    assert build(entry=main)[2] == b"7\n"
    assert read_program(root / "cache", main)["global_types"] is None
    main.write_text(unknown.replace('"discard", unrelated', '"discard", @intrinsic("add", unrelated, 1)'))
    reused = build(entry=main)
    assert reused[1:] == build(False, entry=main)[1:]
    assert reused[0]["function_body_inference_passes"] == 0

    check_global_effect_inputs(root, build)
    check_global_callback_inputs(root, build, source)
    check_global_failure_inputs(root, build, source)
    check_global_layout_inputs(root, build)
    check_rejected_global_inputs(root, build)
    check_global_caller_interfaces(root, build)


def check_global_effect_inputs(root, build):
    for case, effect, call in (
        ("write", '@intrinsic("store", kept, 9)', 'update'),
        ("address", '@intrinsic("return", @intrinsic("address of", kept))',
         '@intrinsic("store", @intrinsic("dereference", update), 9)'),
        ("shadow", '@intrinsic("store", kept, 9)',
         'update\n    @intrinsic("discard", kept)'),
    ):
        main = root / f"global_effect_input_{case}.dl"
        declaration = 'to outer {kept}' if case == "shadow" else 'to outer'
        invocation = 'outer 3' if case == "shadow" else 'outer'
        source = ('@intrinsic("store", kept, 7)\n@intrinsic("store", unrelated, 2)\n'
                  f'function update:\n    globals: kept\n    execute: {effect}\n'
                  'function other:\n    globals: unrelated\n    execute: @intrinsic("discard", unrelated)\n'
                  f'{declaration}:\n    {call}\n{invocation}\n'
                  '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", kept)\n')
        main.write_text(source)
        assert build(entry=main)[2] == b"9\n"
        main.write_text(source.replace('unrelated, 2', 'unrelated, 2.5'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == b"9\n"
        assert edited[0]["function_body_inference_passes"] == 0, case


def check_global_callback_inputs(root, build, declarations):
    main = root / "global_callback_inputs.dl"
    callback = ('function acquire:\n    execute:\n'
                '        @intrinsic("discard", @intrinsic("call", "libc", "puts", @intrinsic("type", "int"), "prepare"))\n'
                '        @intrinsic("return", @intrinsic("cast", @intrinsic("function", "report"), @intrinsic("add pointer depth", @intrinsic("type", "int"))))\n'
                '@intrinsic("call pointer", acquire, @intrinsic("type", "nothing"))\n')
    source = declarations.removesuffix('report\n') + callback
    main.write_text(source)
    assert build(entry=main)[2] == b"prepare\n7\n"
    main.write_text(source.replace('unrelated, 2', 'unrelated, 2.5'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["function_body_inference_passes"] == 0
    main.write_text(source.replace('kept, 7', 'kept, 2147483648'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[2] == b"prepare\n2147483648\n"
    assert changed[0]["function_body_inference_passes"] > 0


def check_global_failure_inputs(root, build, declarations):
    main = root / "global_failure_inputs.dl"
    source = ('@intrinsic("store", kept, "text")\n@intrinsic("store", unrelated, 2)\n'
              'function other:\n    globals: unrelated\n    execute: @intrinsic("discard", unrelated)\n'
              'function broken:\n    globals: kept\n'
              '    execute: @intrinsic("discard", @intrinsic("multiply", kept, 2))\nbroken\n')
    main.write_text(source)
    build(entry=main, success=False)
    main.write_text(source.replace('unrelated, 2', 'unrelated, 2.5'))
    retained = build(entry=main, success=False)
    assert retained[1:] == build(False, entry=main, success=False)[1:]
    assert retained[0]["function_body_inference_passes"] == 0
    assert retained[0]["inference_failures_reused"] == 1
    main.write_text(source.replace('kept, "text"', 'kept, 7'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]["function_body_inference_passes"] == 1

    # Semantic facts saved after failure can have newer global types than IR.
    main = root / "global_failure_native_bridge.dl"
    source = declarations + 'to broken: @intrinsic("discard", missing)\n'
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    changed = source.replace('kept, 7', 'kept, 2147483648')
    main.write_text(changed + 'broken\n')
    build(entry=main, success=False)
    main.write_text(changed + 'broken\nmissing means: 0\n')
    recovered = build(entry=main)
    assert recovered[1:] == build(False, entry=main)[1:]
    assert recovered[2] == b"2147483648\n"

    # Adding an unrelated undefined name must not invalidate a global reader.
    main = root / "global_early_failure.dl"
    main.write_text(declarations)
    assert build(entry=main)[2] == b"7\n"
    main.write_text('@intrinsic("discard", missing)\n' + declarations)
    early_error = build(entry=main, success=False)
    assert early_error[0]["function_body_inference_passes"] == 0
    main.write_text(declarations.replace('unrelated, 2', 'unrelated, 2.5'))
    early_recovery = build(entry=main)
    assert early_recovery[1:] == build(False, entry=main)[1:]
    assert early_recovery[0]["function_body_inference_passes"] == 0, \
        "removing an unrelated undefined name invalidated a global reader"


def check_global_layout_inputs(root, build):
    main = root / "global_layout_inputs.dl"
    source = ('class:\n    patterns: box\n    members: number\n'
              '@intrinsic("store", kept, @intrinsic("construct", box, 7))\n'
              '@intrinsic("store", unrelated, 2)\n'
              'function other:\n    globals: unrelated\n    execute: @intrinsic("discard", unrelated)\n'
              'function report:\n    globals: kept\n    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", @intrinsic("property", kept, "number"), @intrinsic("type", "int", 64)))\n'
              'report\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    main.write_text(source.replace('unrelated, 2', 'unrelated, 2.5'))
    reused = build(entry=main)
    assert reused[1:] == build(False, entry=main)[1:]
    assert reused[0]["function_body_inference_passes"] == 0
    main.write_text(source.replace('box, 7)', 'box, 2147483648)'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[2] == b"2147483648\n"
    assert changed[0]["function_body_inference_passes"] == 1


def check_rejected_global_inputs(root, build):
    main = root / "rejected_global_inputs.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'class:\n    patterns: box\n    members: number as integer\n'
              '@intrinsic("store", selector, "text")\n'
              'function probe:\n    globals: selector\n'
              '    execute: @intrinsic("discard", @intrinsic("multiply", selector, 2))\n'
              'convert item:\n    replacement: @intrinsic("construct", box, 1)\n'
              'convert {integer:item}:\n    replacement:\n'
              '        @intrinsic("discard", @intrinsic("function", "probe"))\n'
              '        @intrinsic("construct", box, 2)\n'
              'to report:\n    @intrinsic("store", result, @intrinsic("cast", 7, box))\n'
              '    @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", @intrinsic("property", result, "number"))\n'
              'report\n')
    main.write_text(source)
    first = build(entry=main)
    assert first[1:] == build(False, entry=main)[1:]
    assert first[2] == b"1\n"
    main.write_text(source.replace('selector, "text"', 'selector, 7'))
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:], "global type edit did not invalidate a rejected trial"
    assert changed[2] == b"2\n"
    main.write_text(source)
    reverted = build(entry=main)
    assert reverted[1:] == build(False, entry=main)[1:]
    assert reverted[2] == b"1\n"
