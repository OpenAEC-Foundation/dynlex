"""Cached inference observes and publishes shared source operand grouping."""

import json
from pathlib import Path
from cache_payloads import read_payload


def check_grouping_state(root, build):
    check_grouped_pure_values(root, build)
    check_fixed_grouping_edit(root, build)
    check_recursive_publication(root, build)
    check_completed_negative_observation(root, build)
    check_available_facts(root, build)
    check_completed_callee_materialization(root, build)
    check_exposed_grouping(root, build)
    check_conversion_grouping(root, build)
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
    check_grouping_scope_eviction(root, build, arithmetic, output)


def check_grouping_scope_eviction(root, build, definitions, output):
    main = root / "grouping_scope_eviction.dl"
    cache = root / "grouping_scope_cache"
    main.write_text(definitions + output)
    first = build(entry=main, cache_root=cache)
    assert first[2] == b"20\n"
    manifest = Path(first[0]["cache_entry"])
    program = json.loads(manifest.read_bytes().split(b"\n", 2)[2])
    grouping = read_payload(manifest.with_name(f'payload-{program["payload_keys"]["root_grouping"]}.entry'))
    keys = set(grouping["requires"] + grouping["provides"])
    assert keys and all(len(key) == 64 for key in keys), "completed grouping scopes must have independent products"
    scopes = [manifest.with_name(f"payload-{key}.entry") for key in keys]
    for scope in scopes:
        scope.unlink()
    main.write_text(definitions + output.replace('"%d', '"value %d'))
    repaired = build(entry=main, cache_root=cache)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[2] == b"value 20\n"
    assert repaired[0]["function_body_inference_passes"] > 0
    assert all(scope.exists() for scope in scopes)


def check_fixed_grouping_edit(root, build):
    source = '''to get identity {value}:
    @intrinsic("return", value)
to get grouped {value}:
    @intrinsic("return", identity identity value)
to get answer {value}:
    @intrinsic("return", grouped @intrinsic("add", value, 1))
@intrinsic("discard", grouped 4.0)
@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", answer @intrinsic("command line argument count"))
'''
    for flags in ((), ("-O2",)):
        main = root / ("fixed_grouping_edit_optimized.dl" if flags else "fixed_grouping_edit.dl")
        main.write_text(source)
        assert build(entry=main, flags=flags)[2] == b"1\n"
        main.write_text(source.replace('"add", value, 1', '"add", value, 2'))
        edit = build(entry=main, flags=flags)
        assert edit[1:] == build(False, entry=main, flags=flags)[1:]
        assert edit[2] == b"2\n"
        assert edit[0]["root_execution_reused"]
        assert edit[0]["function_body_inference_passes"] == 1, "fixed prefix chain reinferred the unchanged callee"


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


def check_grouped_pure_values(root, build):
    main = root / "grouped_pure_values.dl"
    source = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get wrapped {real:value}:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get {integer:value} flipped:
    @intrinsic("return", @intrinsic("cast", value, real))
to get {real:value} flipped:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get generic {value}:
    @intrinsic("return", @intrinsic("cast", (wrapped value flipped), integer))
to get query {real:value}:
    @intrinsic("discard", value)
    @intrinsic("return", generic 1.0)
to get query {integer:value}:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get {real:value} converted:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get chosen {value}:
    @intrinsic("return", query value converted)
@intrinsic("discard", chosen 1.0)
@intrinsic("discard", generic 1)
@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", generic 1.0)
'''
    changed = source.replace('"%d\\n", generic 1.0', '"%d\\n", generic 2.0')
    uncommitted = source.replace('@intrinsic("discard", generic 1)\n', '')
    reordered = source.replace('@intrinsic("discard", generic 1)\n', '')
    reordered = reordered.replace('@intrinsic("discard", chosen 1.0)',
                                  '@intrinsic("discard", generic 1)\n@intrinsic("discard", chosen 1.0)')
    for flags in ((), ("-O2",)):
        for edited, expected in ((source, b"0\n"), (changed, b"0\n"),
                                 (uncommitted, b"1\n"), (reordered, b"0\n")):
            main.write_text(edited)
            result = build(entry=main, flags=flags)
            assert result[1:] == build(False, entry=main, flags=flags)[1:]
            assert result[2] == expected, "constant observations crossed operand-grouping contexts"


def check_recursive_publication(root, build):
    main = root / "recursive_publication.dl"
    source = '''flex section if condition:
    replacement: @intrinsic("if", condition)
to get recursive {value}:
    if @intrinsic("less than", value, 1):
        @intrinsic("return", value)
    @intrinsic("return", recursive @intrinsic("subtract", value, 1))
@intrinsic("discard", recursive @intrinsic("command line argument count"))
'''
    for edited in (source, source.replace('"subtract", value, 1', '"subtract", value, 2')):
        main.write_text(edited)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]


def check_completed_negative_observation(root, build):
    main = root / "completed_negative_observation.dl"
    source = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
to get doubled {integer:value}:
    @intrinsic("return", @intrinsic("add", value, value))
to get {integer:value} doubled:
    @intrinsic("return", @intrinsic("add", value, value))
to get generic {value}:
    @intrinsic("return", doubled value doubled)
to get query {real:value}:
    @intrinsic("discard", value)
    @intrinsic("return", generic "bad")
to get query {integer:value}:
    @intrinsic("return", value)
to get {real:value} converted:
    @intrinsic("return", @intrinsic("cast", value, integer))
to get {integer:value} converted:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get chosen {value}:
    @intrinsic("return", query value converted)
@intrinsic("discard", chosen 1.0)
@intrinsic("discard", generic 1)
@intrinsic("discard", chosen 1.0)
'''
    for edited in (source, source + '@intrinsic("discard", chosen 2.0)\n',
                   source.replace('@intrinsic("discard", generic 1)\n', '')):
        main.write_text(edited)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]


def check_available_facts(root, build):
    main = root / "available_facts.dl"
    source = '''integer means: @intrinsic("type", "int", 32)
real means: @intrinsic("type", "float", 64)
boolean means: @intrinsic("type", "bool")
class box:
    members:
        number as integer
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("less than", value, 0))
to get wrapped {real:value}:
    @intrinsic("discard", value)
    @intrinsic("return", 0)
to get {integer:value} flipped:
    @intrinsic("discard", value)
    @intrinsic("return", 1.0)
to get {real:value} flipped:
    @intrinsic("discard", value)
    @intrinsic("return", 1)
to get generic {value}:
    @intrinsic("return", wrapped value flipped)
implicitly convert {box:value}:
    execute:
        @intrinsic("discard", value)
        @intrinsic("return", generic 1.0)
implicitly convert {box:value}:
    execute:
        @intrinsic("discard", value)
        @intrinsic("return", @intrinsic("equal", 0, 1))
to get accepts {boolean:value}:
    @intrinsic("return", value)
to get selected {value}:
    @intrinsic("return", accepts value)
@intrinsic("discard", selected @intrinsic("construct", box, 1))
@intrinsic("discard", generic 1)
'''
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
    main.write_text(source + '''exposed function external entry {integer:value}:
    execute:
        @intrinsic("discard", selected @intrinsic("construct", box, value))
''')
    assert build(entry=main)[1:] == build(False, entry=main)[1:]


def check_completed_callee_materialization(root, build):
    main = root / "completed_callee_materialization.dl"
    source = '''to get identity {value}:
    @intrinsic("return", value)
to get {value} flipped:
    @intrinsic("return", value)
to get nested {value}:
    @intrinsic("return", identity value flipped)
to get caller {value}:
    @intrinsic("return", nested value)
@intrinsic("discard", caller @intrinsic("command line argument count"))
'''
    main.write_text(source)
    initial = build(entry=main)
    assert initial[1:] == build(False, entry=main)[1:]
    main.write_text(source + '@intrinsic("discard", nested @intrinsic("command line argument count"))\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["function_body_inference_passes"] == 0, \
        "materializing a completed callee repeated its first-execution grouping checks"


def check_exposed_grouping(root, build):
    main = root / "exposed_grouping.dl"
    main.write_text('''integer means: @intrinsic("type", "int", 32)
to get identity {value}:
    @intrinsic("return", value)
to get {value} flipped:
    @intrinsic("return", value)
exposed function entry {integer:value}:
    execute:
        @intrinsic("discard", identity value flipped)
''')
    initial = build(entry=main)
    assert initial[1:] == build(False, entry=main)[1:]
    states = published_grouping_states(initial)
    assert states and all(state[2] for state in states), "completed exposed grouping was stored as unchecked"


def published_grouping_states(result):
    manifest = json.loads(Path(result[0]["cache_entry"]).read_bytes().split(b"\n", 2)[2])
    directory = Path(result[0]["cache_entry"]).parent
    references = read_payload(directory / ("payload-" + manifest["payload_keys"]["inference_summaries"] + ".entry"))
    states = []
    for variants in references.values():
        for reference in variants.values():
            record = json.loads((directory / ("inference-summary-" + reference["payload"] + ".entry"))
                                .read_bytes().split(b"\n", 2)[2])["summary"]
            states.extend(transition[1] for transition in record["grouping"]["groupings"].values() if transition[1] is not None)
    return states


def check_conversion_grouping(root, build):
    main = root / "conversion_grouping.dl"
    main.write_text('''integer means: @intrinsic("type", "int", 32)
class box:
    members:
        number as integer
class target:
    members:
        number as integer
to get wrapped {integer:value}:
    @intrinsic("return", @intrinsic("add", value, 10))
to get {integer:value} flipped:
    @intrinsic("return", @intrinsic("multiply", value, 2))
implicitly convert {integer:value}:
    execute:
        @intrinsic("return", @intrinsic("construct", box, wrapped value flipped))
implicitly convert {integer:value}:
    execute:
        @intrinsic("return", @intrinsic("construct", target, value))
to take {target:value}:
    @intrinsic("discard", value)
to take box {box:value}:
    @intrinsic("discard", value)
take 1
''')
    initial = build(entry=main)
    assert initial[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + "take box 1\n")
    selected = build(entry=main)
    assert selected[1:] == build(False, entry=main)[1:]
    assert selected[1], "selected conversion must check and report ambiguous operand grouping"
    main.write_text(main.read_text().removesuffix("take box 1\n"))
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    assert not removed[1], "removing the selected conversion must remove its ambiguity warning"
