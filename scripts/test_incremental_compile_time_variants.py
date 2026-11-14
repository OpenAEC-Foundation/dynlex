"""Compile-time arguments retain separate dependency and inference units."""

from test_incremental_program_cache import read_program


def check_compile_time_variant_cascade(root, build):
    main = root / "compile_time_variant_cascade.dl"
    source = (
        'type means: @intrinsic("type", "type")\n'
        'integer means: @intrinsic("type", "int")\n'
        'boolean means: @intrinsic("type", "bool")\n'
        'decimal means: @intrinsic("type", "float", 64)\n'
        'function [selected] {integer:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'function [selected] {boolean:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", @intrinsic("cast", value, integer), 10))\n'
        'function [forward] {value} [as] {type:destination}:\n    execute:\n'
        '        @intrinsic("return", selected @intrinsic("cast", value, destination))\n'
    )
    for destination in ("integer", "boolean"):
        source += (
            '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%g\\n", '
            f'@intrinsic("cast", (forward @intrinsic("command line argument count") as {destination}), decimal)))\n'
        )
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n10\n"
    changed = source.replace('@intrinsic("add", value, 1)',
                             '@intrinsic("add", @intrinsic("cast", value, decimal), 1.5)')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[2] == b"1.5\n10\n"
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["preinferred_instantiations"] == 2, \
        f"the type-argument caller was not preinferred independently: {edited[0]}"
    assert edited[0]["native_functions_lowered"] == 3
    assert edited[0]["functions_ir_reused"] >= 2
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [forward]"))
    assert len(program["inference_summaries"][path]) == 2
    assert "*" not in program["section_dependency_graph"]["instantiations"][path]

    main.write_text(changed.replace('decimal), 1.5)', 'decimal), 2.5)'))
    stable = build(entry=main)
    assert stable[2] == b"2.5\n10\n"
    assert stable[0]["root_execution_reused"]
    assert stable[0]["preinferred_instantiations"] == 1
    assert stable[0]["native_functions_lowered"] == 1
    assert stable[1:] == build(False, entry=main)[1:]
    check_scalar_compile_time_variants(root, build)
    check_compile_time_return_values(root, build)
    check_retained_pure_observations(root, build)
    check_unused_pure_summary_retention(root, build)
    check_meta_constraint_name_stability(root, build)
    check_wide_argument_name_stability(root, build)
    check_compile_time_only_calls(root, build)
    check_constant_call_promoted_to_runtime(root, build)
    check_nested_new_pure_arguments(root, build)


def check_scalar_compile_time_variants(root, build):
    main = root / "scalar_compile_time_variants.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'small means: @intrinsic("type", "int", 32)\n'
        'large means: @intrinsic("type", "int", 64)\n'
        'decimal means: @intrinsic("type", "float", 64)\n'
        'fixed integer means: @intrinsic("fix", integer)\n'
        'function [selected] {small:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'function [selected] {large:value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 10))\n'
        'function [forward] {value} [using] {fixed integer:width}:\n    execute:\n'
        '        @intrinsic("return", selected @intrinsic("cast", value, @intrinsic("type", "int", width)))\n'
    )
    for width in (32, 64):
        source += ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%g\\n", '
                   f'@intrinsic("cast", (forward @intrinsic("command line argument count") using {width}), decimal)))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n10\n"
    changed = source.replace('@intrinsic("add", value, 1)',
                             '@intrinsic("add", @intrinsic("cast", value, decimal), 1.5)')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[2] == b"1.5\n10\n"
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["preinferred_instantiations"] == 2
    assert edited[0]["functions_ir_reused"] >= 2
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [forward]"))
    variants = list(program["inference_summaries"][path].values())
    assert len(variants) == 2 and variants[0]["argument_types"] == variants[1]["argument_types"]
    assert variants[0]["compile_time_parameters"] != variants[1]["compile_time_parameters"]
    assert all("width" in variant["parameters"]["required_compile_time"] for variant in variants)
    main.write_text(changed.replace('decimal), 1.5)', 'decimal), 2.5)'))
    stable = build(entry=main)
    assert stable[2] == b"2.5\n10\n"
    assert stable[0]["root_execution_reused"] and stable[0]["preinferred_instantiations"] == 1
    assert stable[0]["native_functions_lowered"] == 1
    assert stable[1:] == build(False, entry=main)[1:]


def check_compile_time_return_values(root, build):
    main = root / "compile_time_return_values.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'to get chosen type:\n'
        '    @intrinsic("return", @intrinsic("type", "int", 32))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        '@intrinsic("cast", @intrinsic("size of", chosen type), integer)))\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"4\n"
    changed = source.replace('"discard", 0)', '"discard", 1)')
    main.write_text(changed)
    reused = build(entry=main)
    assert reused[2] == b"4\n"
    assert reused[0]["pure_value_summary_hits"] > 0, "type-valued result was not restored"
    assert reused[1:] == build(False, entry=main)[1:]
    stable_source = changed.replace('to get chosen type:\n', 'to get chosen type:\n    @intrinsic("discard", 2)\n')
    main.write_text(stable_source)
    stable = build(entry=main)
    assert stable[2] == b"4\n" and stable[0]["root_execution_reused"]
    assert stable[0]["preinfer_pure_values_checked"] == 1
    assert stable[0]["native_functions_lowered"] == 0 and stable[0]["link_ms"] == 0
    assert stable[1:] == build(False, entry=main)[1:]
    main.write_text(stable_source.replace('"type", "int", 32)', '"type", "int", 64)'))
    edited = build(entry=main)
    assert edited[2] == b"8\n" and not edited[0]["root_execution_reused"]
    assert edited[1:] == build(False, entry=main)[1:]


def check_retained_pure_observations(root, build):
    main = root / "retained_pure_observations.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
        'function [compute] {value}:\n    execute:\n'
        '        if @intrinsic("equal", value, 4):\n            @intrinsic("return", 1)\n'
        '        @intrinsic("return", 2)\n'
        'to retained caller:\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", compute 4))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        'compute @intrinsic("command line argument count")))\n'
        'retained caller\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n1\n"
    added_call = ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                  '"%d\\n", compute 5))\n')
    main.write_text(source + added_call)
    added = build(entry=main)
    assert added[2] == b"2\n1\n2\n"
    assert added[0]["functions_ir_reused"] >= 1
    assert added[1:] == build(False, entry=main)[1:]
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [compute]"))
    summary = next(iter(program["inference_summaries"][path].values()))
    arguments = {value[0][0][1] for value in summary["pure_values"]}
    assert arguments == {4, 5}, "a new observed argument discarded the value used by retained caller IR"
    main.write_text((source + added_call).replace('"return", 1)', '"return", 3)'))
    edited = build(entry=main)
    assert edited[2] == b"2\n3\n2\n", "retained caller kept a stale folded return value"
    assert edited[1:] == build(False, entry=main)[1:]


def check_meta_constraint_name_stability(root, build):
    main = root / "meta_constraint_name_stability.dl"
    source = (
        'type means: @intrinsic("type", "type")\n'
        'integer means: @intrinsic("type", "int")\n'
        'small means: @intrinsic("type", "int", 32)\n'
        'function [forward] {value} [as] {type:destination}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, @intrinsic("cast", '
        '@intrinsic("size of", destination), integer)))\n'
    )
    calls = [(' @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
              f'forward @intrinsic("command line argument count") as {kind}))\n').lstrip()
             for kind in ("integer", "small")]
    main.write_text(source + "".join(calls))
    assert build(entry=main)[2] == b"4\n4\n"
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [forward]"))
    names = [name for name, owner in program["function_sections"].items() if owner == path]
    assert len(names) == 2 and not any(name.endswith(".1") for name in names), \
        f"distinct type constraints needed an order-dependent LLVM symbol suffix: {names}"
    main.write_text(source + calls[1] + "".join(calls))
    edited = build(entry=main)
    assert edited[2] == b"4\n4\n4\n"
    assert edited[0]["native_functions_lowered"] == 1
    assert edited[1:] == build(False, entry=main)[1:]


def check_unused_pure_summary_retention(root, build):
    main = root / "unused_pure_summary_retention.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [compute] {value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'to touch: @intrinsic("discard", 1)\n'
        'touch\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", compute 4))\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"5\n"
    changed = source.replace('"discard", 1)', '"discard", 2)')
    main.write_text(changed)
    reused = build(entry=main)
    assert reused[2] == b"5\n" and reused[0]["root_execution_reused"]
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [compute]"))
    variants = program["inference_summaries"][path]
    graph = program["section_dependency_graph"]
    assert set(variants) <= graph["instantiations"][path].keys(), \
        "retained compile-time-only summaries lost their dependency variants"
    assert not any(owner == path for owner in program["function_sections"].values())
    main.write_text(changed + '@intrinsic("discard", compute 4)\n')
    queried = build(entry=main)
    assert queried[2] == b"5\n" and queried[0]["pure_value_summary_hits"] > 0
    assert queried[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().replace('"add", value, 1)', '"add", value, 2)'))
    edited = build(entry=main)
    assert edited[2] == b"6\n" and edited[1:] == build(False, entry=main)[1:]


def check_compile_time_only_calls(root, build):
    main = root / "compile_time_only_calls.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'fixed integer means: @intrinsic("fix", integer)\n'
        'function [width for] {fixed integer:bits}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("cast", @intrinsic("size of", '
        '@intrinsic("type", "int", bits)), integer))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d %d\\n", '
        'width for 32, width for 64))\n'
        '@intrinsic("discard", 0)\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"4 8\n"
    changed = source.replace('"discard", 0)', '"discard", 1)')
    main.write_text(changed)
    reused = build(entry=main)
    assert reused[2] == b"4 8\n" and reused[0]["pure_value_summary_hits"] >= 2
    main.write_text(changed.replace('    execute:\n', '    execute:\n        @intrinsic("discard", 2)\n'))
    stable = build(entry=main)
    assert stable[2] == b"4 8\n" and stable[0]["root_execution_reused"]
    assert stable[0]["preinferred_instantiations"] == 2
    assert stable[0]["preinfer_pure_values_checked"] == 2
    assert stable[0]["native_functions_lowered"] == 0 and stable[0]["link_ms"] == 0
    assert stable[1:] == build(False, entry=main)[1:]


def check_wide_argument_name_stability(root, build):
    main = root / "wide_argument_name_stability.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'huge means: @intrinsic("type", "int", 128)\n'
        'fixed huge means: @intrinsic("fix", huge)\n'
        'function [forward] {value} [with] {fixed huge:offset}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, @intrinsic("cast", offset, integer)))\n'
    )
    calls = [('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
              f'forward @intrinsic("command line argument count") with {2 ** 100 + offset}))\n')
             for offset in (1, 2)]
    main.write_text(source + "".join(calls))
    assert build(entry=main)[2] == b"1\n2\n"
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["inactive"].items()
                if declaration["header"].startswith("function [forward]"))
    names = [name for name, owner in program["function_sections"].items() if owner == path]
    assert len(names) == 2 and not any(name.endswith(".1") for name in names), \
        f"distinct wide arguments needed an order-dependent LLVM symbol suffix: {names}"
    main.write_text(source + calls[1] + "".join(calls))
    edited = build(entry=main)
    assert edited[2] == b"2\n1\n2\n"
    assert edited[0]["native_functions_lowered"] == 1
    assert edited[1:] == build(False, entry=main)[1:]


def check_constant_call_promoted_to_runtime(root, build):
    definition = (
        'integer means: @intrinsic("type", "int")\n'
        'function [compute] {value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
        'to retained caller:\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", compute 4))\n'
    )
    constant = ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
                'compute 4))\n')
    runtime = ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
               'compute @intrinsic("command line argument count")))\n')
    for constant_first in (False, True):
        main = root / f"constant_call_promoted_to_runtime_{constant_first}.dl"
        prefix = constant if constant_first else ""
        main.write_text(definition + prefix + 'retained caller\n')
        assert build(entry=main)[2] == b"5\n" * (1 + int(constant_first))
        promoted = definition + prefix + runtime + 'retained caller\n'
        main.write_text(promoted)
        edited = build(entry=main)
        assert edited[2] == (b"5\n" if constant_first else b"") + b"1\n5\n"
        assert edited[0]["functions_ir_reused"] >= 1
        assert edited[1:] == build(False, entry=main)[1:]
        main.write_text(promoted.replace('"add", value, 1)', '"add", value, 2)'))
        changed = build(entry=main)
        assert changed[2] == (b"6\n" if constant_first else b"") + b"2\n6\n", \
            "runtime promotion lost the old caller's constant observation"
        assert changed[1:] == build(False, entry=main)[1:]


def check_nested_new_pure_arguments(root, build):
    main = root / "nested_new_pure_arguments.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [leaf] {value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, @intrinsic("cast", '
        '@intrinsic("size of", @intrinsic("type", "int", 32)), integer)))\n'
        'function [relay] {value}:\n    execute:\n        @intrinsic("return", leaf value)\n'
        'to retained caller:\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", relay 4))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        'leaf @intrinsic("command line argument count")))\n'
        'retained caller\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"4\n8\n"
    main.write_text(source + '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                    '"%d\\n", @intrinsic("cast", @intrinsic("size of", '
                    '@intrinsic("array", relay 5, integer)), integer)))\n')
    edited = build(entry=main)
    assert edited[2] == b"4\n8\n36\n"
    assert edited[1:] == build(False, entry=main)[1:]
