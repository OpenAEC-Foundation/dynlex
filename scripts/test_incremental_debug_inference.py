"""Debug semantic reuse keeps typed bodies portable and regenerates debug IR."""


def check_debug_inference_units(root, build):
    main = root / "debug_inference_units.dl"
    source = ('to get first {seed}:\n'
              '    @intrinsic("return", @intrinsic("add", seed, 1))\n'
              'to get second {seed}:\n'
              '    @intrinsic("store", local, @intrinsic("add", seed, 2))\n'
              '    @intrinsic("return", local)\n'
              '@intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d %d\\n", '
              'first @intrinsic("command line argument count"), '
              'second @intrinsic("command line argument count"))\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    assert build(entry=main, flags=("-g",))[2] == b"1 2\n"
    changed = source.replace('"discard", 0', '"discard", 1')
    for text, expected, passes in (
        (changed, b"1 2\n", 0),
        (changed.replace('"add", seed, 1', '"add", seed, 3'), b"3 2\n", 1),
        ("# moved source\n\n" + changed.replace('"add", seed, 1', '"add", seed, 3'), b"3 2\n", 0),
    ):
        main.write_text(text)
        edited = build(entry=main, flags=("-g",))
        assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
        assert edited[2] == expected
        assert edited[0]["function_body_inference_passes"] == passes, edited[0]
        assert edited[0]["inference_instantiations_reused"] > 0, edited[0]
        assert edited[0]["inference_bodies_loaded"] > 0, edited[0]
        assert not edited[0]["functions_ir_reused"], edited[0]

    missing = source.replace('"return", local', '"return", absent')
    for text, success in ((missing, False), ("# shifted error\n" + missing, False), (source, True)):
        main.write_text(text)
        edited = build(entry=main, flags=("-g",), success=success)
        assert edited[1:] == build(False, entry=main, flags=("-g",), success=success)[1:]
    check_debug_effect_body(root, build)


def check_debug_effect_body(root, build):
    main = root / "debug_effect_body.dl"
    source = ('to emit {value}:\n'
              '    @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", value)\n'
              'emit @intrinsic("command line argument count")\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    assert build(entry=main, flags=("-g",))[2] == b"0\n"
    for value in (1, 2):
        main.write_text(source.replace('"discard", 0', f'"discard", {value}'))
        edited = build(entry=main, flags=("-g",))
        assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
        assert edited[0]["function_body_inference_passes"] == 0, edited[0]
        assert edited[0]["inference_instantiations_reused"] > 0, edited[0]
        assert edited[0]["inference_bodies_loaded"] > 0, edited[0]
        assert not edited[0]["functions_ir_reused"], edited[0]
