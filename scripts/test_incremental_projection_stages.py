"""Frontend-only edits preserve the later stages needed by subsequent builds."""

from test_incremental_program_cache import read_program


def check_comment_inactive_active_sequence(root, build):
    main = root / "projection_stages.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'function compute {integer:item}:\n'
              '    execute:\n'
              '        @intrinsic("return", @intrinsic("add", item, 10))\n'
              'to idle: @intrinsic("discard", 1)\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "printf", integer, "%d\\n", compute 2))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"12\n"
    prior = read_program(root / "cache", main)["all_function_projection"]
    commented = source.replace('to idle:', '\n# A comment between definitions.\nto idle:')
    main.write_text(commented)
    assert build(entry=main)[0]["frontend_reused"]
    main.write_text(commented.replace('"discard", 1)', '"discard", 2)'))
    inactive = build(entry=main)
    assert inactive[0]["type_inference_reused"]
    projection = read_program(root / "cache", main)["all_function_projection"]
    for field in ("constraint_probe_schedule", "class_instantiations"):
        assert projection[field] == prior[field], "a frontend-only edit discarded inferred stage data"
    compute = next(path for path, entry in prior["inactive"].items()
                   if "compute {integer:item}:" in entry["header"])
    assert projection["bodies"][compute] == prior["bodies"][compute], \
        "a comment changed an executable function's body fingerprint"
    main.write_text(main.read_text().replace('"add", item, 10', '"add", item, 20'))
    active = build(entry=main)
    assert active[2] == b"22\n"
    assert active[1:] == build(False, entry=main)[1:]


def check_nested_inactive_stage_retention(root, build):
    main = root / "nested_projection_stages.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'function compute {integer:item}:\n'
              '    execute:\n'
              '        to idle: @intrinsic("discard", 1)\n'
              '        @intrinsic("return", @intrinsic("add", item, 10))\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "printf", integer, "%d\\n", compute 2))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"12\n"
    prior = read_program(root / "cache", main)["all_function_projection"]
    main.write_text(source.replace('"discard", 1)', '"discard", 2)'))
    inactive = build(entry=main)
    assert inactive[0]["type_inference_reused"]
    projection = read_program(root / "cache", main)["all_function_projection"]
    for field in ("constraint_probe_schedule", "class_instantiations"):
        assert projection[field] == prior[field], "a rebuilt frontend projection lost inferred stage data"
    main.write_text(main.read_text().replace('"add", item, 10', '"add", item, 20'))
    active = build(entry=main)
    assert active[2] == b"22\n"
    assert active[1:] == build(False, entry=main)[1:]
