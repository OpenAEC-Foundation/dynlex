"""Only unread flex bodies may be masked from inference input validation."""

from test_incremental_program_cache import read_program


def check_inactive_flex_inference(root, build):
    main = root / "inactive_flex_inference.dl"
    source = ('unused means: 1\n'
              'label means: "ready"\n'
              'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", '
              '@intrinsic("type", "int"), label))\nreport\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"ready\n"
    changed = source.replace('unused means: 1', 'unused means: 2')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["type_inference_reused"], "an unread flex edit reran inference"
    assert edited[0]["function_body_inference_passes"] == 0
    assert edited[0]["preinferred_instantiations"] == 0
    assert edited[0]["objects_built"] == 0 and edited[0]["link_ms"] == 0

    main.write_text(changed.replace('"ready"', '"changed"'))
    active = build(entry=main)
    assert active[1:] == build(False, entry=main)[1:]
    assert active[2] == b"changed\n"
    assert not active[0]["type_inference_reused"], "an active expansion was treated as unread"
    check_inactive_flex_dependencies(root, build)


def check_inactive_flex_dependencies(root, build):
    main = root / "inactive_flex_types.dl"
    source = ('word means: @intrinsic("type", "int", 8)\n'
              'to get echo {word:value}:\n    @intrinsic("return", value)\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "int"), 1, "%lld\\n", @intrinsic("size of", '
              '@intrinsic("function return type", @intrinsic("function", "echo $")))))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n"
    main.write_text(source.replace('"int", 8', '"int", 64'))
    typed = build(entry=main)
    assert typed[1:] == build(False, entry=main)[1:]
    assert typed[2] == b"8\n", "a constraint type provider was masked as unread"

    # The field-type read happens while preparing the class, before its generated
    # property accessor is called by entry-point execution.
    main = root / "inactive_flex_class_fields.dl"
    source = ('word means: @intrinsic("type", "int", 8)\n'
              'class:\n    patterns:\n        box\n    members:\n        payload as word\n        spare as word\n'
              '@intrinsic("store", {item}, @intrinsic("construct", box, 1, 1))\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "int"), 1, "%lld\\n", @intrinsic("size of", '
              '@intrinsic("type of", item\'s payload))))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n"
    inactive = read_program(root / "cache", main)["inactive_projection"]["inactive"]
    assert not inactive, "a generated accessor with no source body entered the source-mask set"
    main.write_text(source.replace('"int", 8', '"int", 64'))
    field = build(entry=main)
    assert field[1:] == build(False, entry=main)[1:]
    assert field[2] == b"8\n", "a generated property's field-type dependency was lost"

    main = root / "inactive_flex_diagnostics.dl"
    source = 'answer means: 7\n@intrinsic("discard", answer)\n'
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('answer means: 7', 'answer means: @intrinsic("multiply", 1, "wrong")'))
    invalid = build(entry=main, success=False)
    assert invalid[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
