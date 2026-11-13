"""Global input edits stop cascading when a callee's concrete interface is unchanged."""


import struct
from test_incremental_program_cache import read_program

def check_global_caller_interfaces(root, build):
    for check in (check_global_callers, check_saved_invocations, check_global_caller_body_edit,
                  check_retained_caller_dependencies, check_caller_result_dependencies, check_global_layout_callers,
                  check_global_signature_change, check_changed_entry_skips_unused_queries,
                  check_new_grouping_query_reuse, check_nested_grouping_query_reuse):
        check(root, build)


def check_changed_entry_skips_unused_queries(root, build):
    main = root / "changed_entry_unused_query.dl"
    main.write_text('to work: @intrinsic("discard", 1)\nwork\n')
    assert build(entry=main)[0]["function_body_inference_passes"] == 1
    main.write_text('to work: @intrinsic("discard", 2)\n@intrinsic("discard", 0)\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["function_body_inference_passes"] == 0, \
        "entry edits inferred an old native function which is no longer called"
    assert edited[0]["source_function_body_clones"] == 0
    assert edited[0]["inference_bodies_encoded"] == 0
    assert edited[0]["inference_bodies_loaded"] == 0


def check_new_grouping_query_reuse(root, build):
    main = root / "new_grouping_query.dl"
    source = ('number means: @intrinsic("type", "int", 32)\n'
              'function {left} - {right}:\n    replacement: @intrinsic("subtract", left, right)\n'
              'function {value} adjusted:\n    execute: @intrinsic("return", @intrinsic("add", value, 1))\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int", 32), 1, "%d\\n", '
              '@intrinsic("cast", @intrinsic("cast", @intrinsic("command line argument count"), number) - '
              '@intrinsic("cast", 1, number) adjusted, @intrinsic("type", "int", 32))))\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    changed = source.replace('number means: @intrinsic("type", "int", 32)',
                             'number means: @intrinsic("type", "int", 64)')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[1].count("Warning:") == 1
    assert edited[0]["function_body_inference_passes"] == 1, \
        "grouping trials repeated a newly completed concrete query"
    assert edited[0]["source_function_body_clones"] == 1
    assert edited[0]["inference_products_restored_current_build"] > 0
    assert edited[0]["inference_bodies_encoded"] == 1, \
        "new pure observations encoded an unchanged restored body"
    main.write_text(changed.replace('"cast", 1, number', '"cast", 2, number'))
    new_value = build(entry=main)
    assert new_value[1:] == build(False, entry=main)[1:]
    assert new_value[0]["function_body_inference_passes"] == 0
    assert new_value[0]["inference_bodies_encoded"] == 0


def check_nested_grouping_query_reuse(root, build):
    main = root / "nested_grouping_query.dl"
    source = ('integer means: @intrinsic("type", "int", 32)\n'
              'function {left} - {right}:\n    replacement: @intrinsic("subtract", left, right)\n'
              'function {value} adjusted:\n    execute: @intrinsic("return", @intrinsic("add", value, 1))\n'
              'function work:\n    execute:\n'
              '        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
              '@intrinsic("cast", @intrinsic("cast", @intrinsic("command line argument count"), integer) - '
              '@intrinsic("cast", 1, integer) adjusted, integer)))\n'
              'work\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"0\n" and cold[1:] == build(False, entry=main)[1:]
    changed = (source.replace('"add", value, 1', '"add", value, 3')
               .replace('"cast", 1, integer', '"cast", 2, integer')
               + '@intrinsic("discard", 0)\n')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[2] == b"1\n" and edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["function_body_inference_passes"] == 2, \
        "a nested probe repeated a completed query already present in the previous executable"
    assert edited[0]["source_function_body_clones"] == 2
    assert edited[0]["inference_products_restored_current_build"] > 0


def check_global_callers(root, build):
    main = root / "global_caller_interfaces.dl"
    source = ('@intrinsic("store", count, 7)\n'
              'function report:\n    globals: count\n    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", count, @intrinsic("type", "int", 64)))\n'
              'to middle: report\nto outer: middle\nouter\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    program = read_program(root / "cache", main)
    summaries = [summary for variants in program["inference_summaries"].values()
                 for summary in variants.values()]
    assert len(summaries) == 3
    assert sum(bool(summary["global_inputs"]) for summary in summaries) == 1, \
        "callee effects became direct global type inputs of callers"
    units = [unit for path, variants in program["section_dependency_graph"]["inference_units"].items()
             if path != "root" for unit in variants.values()]
    assert sum(bool(unit["direct_global_inputs"]) for unit in units) == 1
    assert sum(bool(unit["global_inputs"]) for unit in units) == 3
    main.write_text(source.replace('count, 7', 'count, 2147483648'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"2147483648\n"
    assert edited[0]["function_body_inference_passes"] == 1, \
        "a global type edit invalidated callers despite an unchanged callee interface"
    assert edited[0]["source_function_body_clones"] == 1
    assert edited[0]["deferred_query_probes"] == 1
    assert edited[0]["deferred_query_interfaces_preserved"] == 2
    assert edited[0]["inference_products_restored_current_build"] > 0
    assert edited[0]["inference_body_transfers"] > 0
    for value, passes in ((7, 1), (2147483648, 1), (2147483649, 0)):
        main.write_text(source.replace('count, 7', f'count, {value}'))
        repeated = build(entry=main)
        assert repeated[1:] == build(False, entry=main)[1:]
        assert repeated[2] == f"{value}\n".encode()
        assert repeated[0]["function_body_inference_passes"] == passes



def check_saved_invocations(root, build):
    main = root / "saved_invocation.dl"
    source = ('function:\n    patterns:\n'
              '        mix {zeta} with {alpha}\n        blend {zeta} and {alpha}\n'
              '    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d:%d\\n", zeta, alpha)\n'
              'to relay: blend 7 and 2\nrelay\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"7:2\n"
    program = read_program(root / "cache", main)
    invocations = [summary["invocation"] for variants in program["inference_summaries"].values()
                   for summary in variants.values()]
    assert [1, ["zeta", "alpha"], []] in [invocation[:3] for invocation in invocations], \
        "the selected definition or argument order was not retained"
    assert all(len(invocation) == 4 and isinstance(invocation[3], str) and invocation[3] for invocation in invocations), \
        "the compiled callable declaration was not retained"
    main.write_text(source.replace('    execute:\n', '    execute:\n        @intrinsic("discard", 0)\n'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"7:2\n"
    assert edited[0]["function_body_inference_passes"] == 1
    assert edited[0]["source_function_body_clones"] == 1


def check_global_caller_body_edit(root, build):
    main = root / "global_caller_body_edit.dl"
    source = ('@intrinsic("store", count, 7)\n'
              'function report:\n    globals: count\n    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", count)\n'
              'to middle:\n    report\nto outer: middle\nouter\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    main.write_text(source.replace('    report\n', '    report\n    @intrinsic("discard", 0)\n'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"7\n"
    assert edited[0]["function_body_inference_passes"] == 1


def check_retained_caller_dependencies(root, build):
    main = root / "retained_caller_dependencies.dl"
    source = ('@intrinsic("store", count, 7)\n'
              'function report:\n    globals: count\n    execute:\n'
              '        @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", count, @intrinsic("type", "int", 64)))\n'
              'to middle: report\nto outer: middle\nouter\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"7\n"
    changed = source.replace('to outer: middle\n', 'to outer:\n    middle\n    @intrinsic("discard", 0)\n')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    program = read_program(root / "cache", main)
    units = [unit for path, variants in program["section_dependency_graph"]["inference_units"].items()
             if path != "root" for unit in variants.values()]
    assert sum(bool(unit["global_inputs"]) for unit in units) == 3, \
        "restoring a callee inside a probe lost its saved dependencies"
    main.write_text(changed.replace('count, 7', 'count, 2147483648'))
    widened = build(entry=main)
    assert widened[1:] == build(False, entry=main)[1:]
    assert widened[2] == b"2147483648\n"
    assert widened[0]["function_body_inference_passes"] == 1


def check_caller_result_dependencies(root, build):
    pointer_size = f"{struct.calcsize('P')}\n".encode()
    for name, expression, before, after, passes in (
        ("return_type", 'count', b"7\n", b"2147483648\n", 2),
        ("changed_value", '@intrinsic("size of", @intrinsic("type of", count))', b"4\n", b"8\n", 2),
        ("same_value", '@intrinsic("size of", @intrinsic("add pointer depth", @intrinsic("type of", count)))',
         pointer_size, pointer_size, 1),
    ):
        main = root / f"caller_result_{name}.dl"
        source = ('@intrinsic("store", count, 7)\n'
                  'function reading:\n    globals: count\n'
                  f'    execute: @intrinsic("return", {expression})\n'
                  'to middle:\n'
                  '    @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", reading, @intrinsic("type", "int", 64)))\n'
                  'to outer: middle\nouter\n')
        main.write_text(source)
        assert build(entry=main)[2] == before
        main.write_text(source.replace('count, 7', 'count, 2147483648'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:], name
        assert edited[2] == after, name
        assert edited[0]["function_body_inference_passes"] == passes, name


def check_global_layout_callers(root, build):
    main = root / "global_layout_callers.dl"
    source = ('class:\n    patterns: box\n    members:\n        flag\n        number\n'
              '@intrinsic("store", kept, @intrinsic("construct", box, @intrinsic("equal", 1, 2), 7))\n'
              'function acquire:\n    globals: kept\n'
              '    execute: @intrinsic("return", @intrinsic("address of", kept))\n'
              'to middle:\n    @intrinsic("store", pointer, acquire)\n'
              '    @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%lld\\n", @intrinsic("cast", @intrinsic("property", @intrinsic("dereference", pointer), "number"), @intrinsic("type", "int", 64)))\n'
              'to outer: middle\nouter\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"7\n"
    assert cold[0]["inference_class_states_captured"] > 0
    main.write_text(source.replace('box, @intrinsic("equal", 1, 2), 7', 'box, @intrinsic("equal", 1, 2), 2147483648'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"2147483648\n"
    assert edited[0]["function_body_inference_passes"] == 2


def check_global_signature_change(root, build):
    main = root / "global_signature_change.dl"
    source = ('number kind means: @intrinsic("type", "int", 32)\n'
              '@intrinsic("store", count, 7)\n'
              'function accept {number kind:item}:\n    globals: count\n'
              '    execute: @intrinsic("discard", @intrinsic("add", item, count))\n'
              'to middle: accept 1\nto outer: middle\nouter\n')
    main.write_text(source)
    build(entry=main)
    changed = source.replace('"type", "int", 32', '"type", "float", 64')
    main.write_text(changed)
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(changed.replace('accept 1\n', 'accept (@intrinsic("cast", 1, @intrinsic("type", "float", 64)))\n'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
