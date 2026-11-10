"""LLVM optimization rechecks changed units and their imported callees."""

from test_incremental_program_cache import read_program


def check_optimization_units(root, build):
    main = root / "optimization_units.dl"
    source = ('integer means: @intrinsic("type", "int")\n' +
              ''.join(f'to part{index}: @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                      f'integer, "part{index}"))\n' for index in range(6)) +
              ''.join(f'part{index}\n' for index in range(6)))
    flags = ("-O2",)
    main.write_text(source)
    cold = build(entry=main, flags=flags)
    assert cold[0]["optimization_units_built"] >= 6
    assert cold[0]["native_functions"] < 6, \
        "fully inlined internal functions survived optimization"
    main.write_text(source.replace('"part2"', '"edited"'))
    edited = build(entry=main, flags=flags)
    assert edited[0]["optimization_units_reused"] >= 4, edited[0]
    assert edited[0]["optimization_input_units_reused"] >= 4, edited[0]
    assert edited[0]["optimization_units_built"] < cold[0]["optimization_units_built"], edited[0]
    assert edited[1:] == build(False, entry=main, flags=flags)[1:]
    assert edited[2].splitlines() == [b"part0", b"part1", b"edited", b"part3", b"part4", b"part5"]
    main.write_text(source.replace('"part2"', '"edited again"'))
    repeated = build(entry=main, flags=flags)
    assert repeated[0]["optimization_units_reused"] >= 4, repeated[0]
    assert repeated[1:] == build(False, entry=main, flags=flags)[1:]

    snapshot = read_program(root / "cache", main)["optimization_units"]
    directory = root / "cache" / "native-v1"
    paths = {directory / f"optimization-{key}.entry" for key in snapshot["outputs"].values()}
    originals = {path: path.read_bytes() for path in paths}
    try:
        for path, content in originals.items():
            path.write_bytes(content[:-1] + bytes([content[-1] ^ 1]))
        assert build(entry=main, flags=flags)[0]["frontend_reused"], "unchanged build loaded optimization payloads"
        main.write_text(source.replace('"part2"', '"third"'))
        assert "corrupt cache entry" in build(entry=main, flags=flags, success=False)[1]
    finally:
        for path, content in originals.items():
            path.write_bytes(content)
    assert build(entry=main, flags=flags)[1:] == build(False, entry=main, flags=flags)[1:]

    # Raw loads/stores exceed ThinLTO's import budget; local simplification makes
    # this body small enough to inline before the final liveness check.
    main = root / "optimization_prelink.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to report value:\n'
              '    @intrinsic("store", local, value)\n' +
              '    @intrinsic("store", local, @intrinsic("add", local, 0))\n' * 32 +
              '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d", local))\n'
              'report @intrinsic("call", "libc", "rand", integer)\n')
    main.write_text(source)
    simplified = build(entry=main, flags=flags)
    assert simplified[0]["native_functions"] == 1, \
        "raw input size prevented importing a locally trivial function"
    assert simplified[1:] == build(False, entry=main, flags=flags)[1:]
