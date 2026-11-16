"""Debug builds reuse source resolution and regenerate current source locations."""

def check_debug_source_units(root, build):
    from test_incremental_debug_inference import check_debug_inference_units
    check_debug_inference_units(root, build)
    main = root / "debug_source_units.dl"
    source = ('to get next value:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              'to report {value}:\n'
              '    @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", next value)\n'
              'report @intrinsic("command line argument count")\n')
    flags = ("-g",)
    main.write_text(source)
    cold = build(entry=main, flags=flags)
    assert cold[2] == b"1\n"
    for prefix, value in (("", 2), ("# shifted source\n\n", 3)):
        main.write_text(prefix + source.replace('"add", value, 1', f'"add", value, {value}'))
        edited = build(entry=main, flags=flags)
        assert edited[1:] == build(False, entry=main, flags=flags)[1:]
        assert edited[2] == f"{value}\n".encode()
        assert edited[0]["classification_units_reused"] > 0, edited[0]
        assert edited[0]["body_resolution_units_reused"] > 0, edited[0]
        assert edited[0]["cached_body_resolution_references_skipped"] > 0, edited[0]
        assert not edited[0]["type_inference_reused"]
        assert not edited[0]["functions_ir_reused"]

    missing = source.replace('"add", value, 1', '"add", missing, 1')
    for text, success in ((missing, False), (source, True),
                          (source.replace('"add", value, 1', '"add", 0, 1'), True),
                          (source, True)):
        main.write_text(text)
        edited = build(entry=main, flags=flags, success=success)
        assert edited[1:] == build(False, entry=main, flags=flags, success=success)[1:]
