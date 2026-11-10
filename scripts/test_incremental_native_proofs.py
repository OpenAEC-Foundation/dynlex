"""Optimized native dependencies follow cached output and current bindings."""


def check_optimized_native_dependencies(root, build):
    main = root / "optimized_native_dependencies.dl"
    source = ('integer means: @intrinsic("type", "int")\n' +
              ''.join(f'exposed function callback{index}:\n    execute:\n'
                      '        @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                      f'integer, "callback{index}"))\n' for index in range(3)) +
              ''.join('@intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
                      f'@intrinsic("function", "callback{index}")))\n' for index in range(3)))
    flags = ("-O2",)
    main.write_text(source)
    cold = build(entry=main, flags=flags)
    assert cold[0]["native_functions"] >= 3, cold[0]
    assert cold[0]["native_function_fingerprints_computed"] == cold[0]["native_functions"], cold[0]
    assert cold[2].splitlines() == [b"callback2", b"callback1", b"callback0"]
    for text in ("edited", "edited again"):
        main.write_text(source.replace('integer, "callback1"', f'integer, "{text}"'))
        edited = build(entry=main, flags=flags)
        assert edited[0]["native_function_dependencies_reused"] >= 2, edited[0]
        assert edited[0]["native_optimized_function_proofs"] >= 2, edited[0]
        assert edited[0]["native_function_fingerprints_computed"] < edited[0]["native_functions"], edited[0]
        assert edited[0]["optimization_function_records_reused"] >= 2, edited[0]
        assert edited[1:] == build(False, entry=main, flags=flags)[1:]
        assert edited[2].splitlines() == [b"callback2", text.encode(), b"callback0"]

    # Rebinding and liveness can change independently of an unchanged callback.
    added = ('exposed function another callback:\n    execute:\n'
             '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "new"))\n' +
             source + '@intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
             '@intrinsic("function", "another callback")))\n')
    main.write_text(added)
    assert build(entry=main, flags=flags)[1:] == build(False, entry=main, flags=flags)[1:]
    main.write_text(source)
    assert build(entry=main, flags=flags)[1:] == build(False, entry=main, flags=flags)[1:]
