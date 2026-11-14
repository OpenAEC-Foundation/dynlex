"""Managed lifecycle units reuse facts and invalidate on body or layout edits."""

from test_incremental_program_cache import read_program


def aggregate_source_inputs(program):
    return {path: variants["*"]["sources"]
            for path, variants in program["section_dependency_graph"]["inference_units"].items()
            if path != "root" and "*" in variants}


def check_lifecycle_inference_reuse(root, build):
    for flags in ((), ("-O2",)):
        main = root / f"incremental_lifecycle_{len(flags)}.dl"
        source = '''integer means: @intrinsic("type", "int")
floating means: @intrinsic("type", "float")
class:
    patterns:
        box
    members:
        number as integer
    retain:
        @intrinsic("discard", @intrinsic("lifecycle value"))
    release:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", @intrinsic("cast", @intrinsic("property", @intrinsic("lifecycle value"), "number"), integer)))
@intrinsic("store", held, @intrinsic("construct", box, 7))
@intrinsic("discard", held)
@intrinsic("discard", 10)
'''
        main.write_text(source)
        initial = build(entry=main, flags=flags)
        assert initial[2] == b"7\n7\n"
        for old, new, output in (('"discard", 10', '"discard", 11', b"7\n7\n"),
                                 ('box, 7)', 'box, 8)', b"8\n8\n")):
            source = source.replace(old, new)
            main.write_text(source)
            edited = build(entry=main, flags=flags)
            assert edited[2] == output
            assert edited[1:] == build(False, entry=main, flags=flags)[1:]
            assert edited[0]["function_body_inference_passes"] == 0, \
                "unchanged retain and release bodies were inferred after an unrelated edit"
            assert edited[0]["inference_instantiations_reused"] >= 2
        source = source.replace('"%d\\n"', '"value=%d\\n"')
        main.write_text(source)
        changed_body = build(entry=main, flags=flags)
        assert changed_body[2] == b"value=8\nvalue=8\n"
        assert changed_body[1:] == build(False, entry=main, flags=flags)[1:]
        assert changed_body[0]["function_body_inference_passes"] == 1, \
            "editing release also inferred the unchanged retain body"
        main.write_text(source.replace('number as integer', 'number as floating').replace('box, 8)', 'box, 8.0)'))
        layout = build(entry=main, flags=flags)
        assert layout[2] == b"value=8\nvalue=8\n"
        assert layout[1:] == build(False, entry=main, flags=flags)[1:]
        assert layout[0]["function_body_inference_passes"] >= 2, \
            "changed class layout retained lifecycle facts for the previous ABI"
    check_nested_lifecycle_reuse(root, build)


def check_nested_lifecycle_reuse(root, build):
    source = '''integer means: @intrinsic("type", "int")
class:
    patterns:
        inner
    members:
        number as integer
    retain:
        @intrinsic("discard", @intrinsic("lifecycle value"))
    release:
        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "inner"))
class:
    patterns:
        outer
    members:
        number as integer
    retain:
        @intrinsic("discard", @intrinsic("construct", inner, 7))
    release:
        @intrinsic("discard", @intrinsic("construct", inner, 7))
@intrinsic("store", held, @intrinsic("construct", outer, 1))
@intrinsic("discard", held)
@intrinsic("discard", 10)
'''
    for flags in ((), ("-O2",)):
        main = root / f"nested_incremental_lifecycle_{len(flags)}.dl"
        main.write_text(source)
        initial = build(entry=main, flags=flags)
        assert initial[2] and set(initial[2].splitlines()) == {b"inner"}
        edited_source = source.replace('"discard", 10', '"discard", 11')
        main.write_text(edited_source)
        reused = build(entry=main, flags=flags)
        assert reused[1:] == build(False, entry=main, flags=flags)[1:] == initial[1:]
        assert reused[0]["function_body_inference_passes"] == 0, \
            "nested lifecycle dependencies were inferred instead of reusing their concrete units"
        inputs = aggregate_source_inputs(read_program(root / "cache", main))
        for message in ("changed", "changed again"):
            changed_source = edited_source.replace('integer, "inner"', f'integer, "{message}"')
            main.write_text(changed_source)
            changed = build(entry=main, flags=flags)
            assert changed[1:] == build(False, entry=main, flags=flags)[1:]
            assert changed[2] and set(changed[2].splitlines()) == {message.encode()}, \
                "an outer cached lifecycle retained the old nested release body"
            assert changed[0]["function_body_inference_passes"] == 1, \
                "an equivalent lifecycle interface re-inferred unchanged outer callers"
            assert changed[0]["preinferred_instantiations"] == 1
            assert aggregate_source_inputs(read_program(root / "cache", main)) == inputs, \
                "a lifecycle body edit changed the source input of its owning class"
        pure = changed_source.replace(
            '@intrinsic("call", "libc", "puts", integer, "changed again")',
            '@intrinsic("lifecycle value")')
        main.write_text(pure)
        effects = build(entry=main, flags=flags)
        assert effects[1:] == build(False, entry=main, flags=flags)[1:]
        assert effects[2] == b""
        assert effects[0]["function_body_inference_passes"] > 1, \
            "a changed lifecycle purity did not propagate to its callers"
        invalid = pure.replace(
            '@intrinsic("discard", @intrinsic("lifecycle value"))', '@intrinsic("return", 7)')
        main.write_text(invalid)
        failed = build(entry=main, flags=flags, success=False)
        assert failed[1:] == build(False, entry=main, flags=flags, success=False)[1:]
        main.write_text(changed_source)
        recovered = build(entry=main, flags=flags)
        assert recovered[1:] == changed[1:] == build(False, entry=main, flags=flags)[1:]
