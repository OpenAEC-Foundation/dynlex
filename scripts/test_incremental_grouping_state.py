"""Cached inference observes and publishes shared source operand grouping."""


def check_grouping_state(root, build):
    check_failed_grouping_candidate(root, build)
    main = root / "grouping_interface.dl"
    interface = '''to get identity {value}:
    @intrinsic("return", value)
to get answer {value}:
    @intrinsic("return", identity identity @intrinsic("add", value, 1))
@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", answer @intrinsic("command line argument count"))
'''
    main.write_text(interface)
    assert build(entry=main)[2] == b"1\n"
    main.write_text(interface.replace('"add", value, 1', '"add", value, 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"2\n"
    assert edited[0]["root_execution_reused"]
    assert edited[0]["function_body_inference_passes"] == 1
    main = root / "grouping_loop_context.dl"
    loop = '''flex section while condition:
    replacement: @intrinsic("loop while", condition)
to get identity {value}:
    @intrinsic("return", value)
to get measured {value}:
    @intrinsic("return", identity identity value)
to use {value}:
    @intrinsic("store", counter, 0)
    while @intrinsic("less than", counter, measured value):
        @intrinsic("store", counter, @intrinsic("add", counter, 1))
use @intrinsic("command line argument count")
'''
    main.write_text(loop)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(loop + '@intrinsic("discard", 0)\n')
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    definitions = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get wrapped {real:value}:
    @intrinsic("discard", value)
    @intrinsic("return", 0)
to get {integer:value} flipped:
    @intrinsic("discard", value)
    @intrinsic("return", 1.0)
to get computed {value}:
    @intrinsic("return", wrapped value flipped)
'''
    integer_call = '@intrinsic("discard", computed 1)\n'
    real_call = '@intrinsic("discard", computed 1.0)\n'
    for name, first, second in (
        ("integer_first", integer_call, real_call),
        ("real_first", real_call, integer_call),
    ):
        folder = root / name
        folder.mkdir()
        main = folder / "main.dl"
        main.write_text(definitions + first)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        main.write_text(definitions + first + first)
        repeated = build(entry=main)
        assert repeated[1:] == build(False, entry=main)[1:]
        assert repeated[0]["function_body_inference_passes"] == 0
        assert repeated[0]["inference_instantiations_reused"] > 0
        main.write_text(definitions + first + second)
        assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
        main.write_text(definitions + second + first)
        assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
        main.write_text(definitions + second)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # The integer call permits both groupings. A preceding real call fixes one
    # without a warning; removing it must restore ordinary ambiguity checking.
    ambiguous = definitions + '''boolean means: @intrinsic("type", "bool")
to get {boolean:value} flipped:
    @intrinsic("discard", value)
    @intrinsic("return", 2.0)
'''
    main = root / "grouping_input.dl"
    main.write_text(ambiguous + real_call + integer_call)
    initial = build(entry=main)
    assert initial[1:] == build(False, entry=main)[1:]
    assert not initial[1]
    main.write_text(ambiguous + integer_call)
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert removed[1]
    assert removed[0]["function_body_inference_passes"] > 0
    for calls in (real_call + integer_call, integer_call + real_call, real_call):
        main.write_text(ambiguous + calls)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

    # A new constant observation needs an executable body. An evicted body
    # must not publish its grouping before ordinary inference checks ambiguity.
    for name, runtime in (("folded", ""), ("native", '@intrinsic("discard", computed '
                         '@intrinsic("command line argument count"))\n')):
        main = root / ("grouping_constant_eviction_" + name + ".dl")
        directory = root / "cache" / "native-v1"
        before = set(directory.glob("inference-body-*.entry"))
        main.write_text(ambiguous + runtime + integer_call)
        initial = build(entry=main)
        assert initial[1:] == build(False, entry=main)[1:]
        bodies = set(directory.glob("inference-body-*.entry")) - before
        assert bodies
        for body in bodies:
            body.unlink()
        main.write_text(ambiguous + integer_call.replace("computed 1", "computed 2"))
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "grouping_caller.dl"
    delegated = definitions + 'to get delegated {value}:\n    @intrinsic("return", computed value)\n'
    for calls, success in ((integer_call, True), (integer_call + real_call, False),
                           (real_call + integer_call, False), (real_call, True)):
        main.write_text(delegated + calls.replace("computed", "delegated"))
        assert build(entry=main, success=success)[1:] == build(False, entry=main, success=success)[1:]

    # Both integer groupings return integers but compute different values.
    # Native IR must depend on the grouping as well as the return/effect types.
    main = root / "grouping_native_result.dl"
    arithmetic = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("add", value, 10))
to get wrapped {real:value}:
    @intrinsic("return", @intrinsic("less than", value, 0.0))
to get {integer:value} flipped:
    @intrinsic("return", @intrinsic("multiply", value, 2))
to get {real:value} flipped:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get computed {value}:
    @intrinsic("return", wrapped value flipped)
'''
    output = ('@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), '
              '1, "%d\\n", computed @intrinsic("command line argument count"))\n')
    main.write_text(arithmetic + real_call + output)
    assert build(entry=main)[2] == b"10\n"
    main.write_text(arithmetic + output)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"20\n"
    main.write_text(arithmetic + real_call + output)
    assert build(entry=main)[2] == b"10\n"
    main.write_text(arithmetic + output + '@intrinsic("discard", missing)\n')
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(arithmetic + output)
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[2] == b"20\n"


def check_failed_grouping_candidate(root, build):
    main = root / "grouping_failed_candidate.dl"
    definitions = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get wrapped {real:value}:
    @intrinsic("discard", value)
    @intrinsic("return", 0)
to get {integer:value} flipped:
    @intrinsic("discard", value)
    @intrinsic("return", 1.0)
to get generic {value}:
    @intrinsic("return", wrapped value flipped)
to get query {real:value}:
    @intrinsic("return", @intrinsic("cast", generic value, integer))
to get query {integer:value}:
    @intrinsic("discard", value)
    @intrinsic("return", @intrinsic("equal", 0, 1))
to get {real:value} converted:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get {integer:value} converted:
    @intrinsic("discard", value)
    @intrinsic("return", @intrinsic("equal", 0, 0))
to get chosen {value}:
    @intrinsic("return", query value converted)
'''
    output = ('@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), '
              '1, "%d\\n", @intrinsic("cast", chosen 1.0, integer))\n')
    main.write_text(definitions + '@intrinsic("discard", generic 1)\n' + output)
    assert build(entry=main)[2] == b"0\n"
    main.write_text(definitions + '@intrinsic("discard", generic 1.0)\n' + output)
    changed = build(entry=main)
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[2] == b"1\n"
