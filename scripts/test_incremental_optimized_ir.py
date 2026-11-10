"""Inference and generated bodies survive optimization and later callee edits."""

def check_optimized_function_reuse(root, build):
    main = root / "optimized_functions.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to changed message:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'to caller message: changed message\n'
              'to stable message:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'caller "argument"\n'
              'stable "stable"\n')
    for optimization in ("-O1", "-O2", "-O3", "-Os", "-Oz"):
        flags = (optimization,)
        main.write_text(source)
        cold = build(entry=main, flags=flags)
        assert cold[1:] == build(False, entry=main, flags=flags)[1:]
        changed = source.replace('to changed message:\n',
                                 'to changed message:\n'
                                 '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "edited"))\n')
        main.write_text(changed)
        edited = build(entry=main, flags=flags)
        assert edited[1:] == build(False, entry=main, flags=flags)[1:]
        assert edited[2].splitlines() == [b"edited", b"argument", b"stable"]
        assert edited[0]["inference_instantiations_reused"] >= 2, (optimization, edited[0])
        assert edited[0]["functions_ir_reused"] >= 2, (optimization, edited[0])
        main.write_text(changed.replace('"edited"', '"again"'))
        repeated = build(entry=main, flags=flags)
        assert repeated[0]["functions_ir_reused"] >= 2, (optimization, repeated[0])
        assert repeated[1:] == build(False, entry=main, flags=flags)[1:]
        assert repeated[2].splitlines() == [b"again", b"argument", b"stable"]

    # libc invokes the exposed callback after main returns. Its body must change
    # when optimization inlines the edited leaf into otherwise reused code.
    source = ('integer means: @intrinsic("type", "int")\n'
              'to changed message:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, message))\n'
              'exposed function callback:\n'
              '    execute:\n'
              '        changed "argument"\n'
              'to arrange callback:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
              '@intrinsic("function", "callback")))\n'
              'to stable: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "stable"))\n'
              'arrange callback\nstable\n')
    main = root / "optimized_callback.dl"
    main.write_text(source)
    flags = ("-O2",)
    assert build(entry=main, flags=flags)[2].splitlines() == [b"stable", b"argument"]
    for message in ("edited", "again"):
        main.write_text(source.replace('to changed message:\n',
                                      'to changed message:\n'
                                      '    @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                                      f'integer, "{message}"))\n'))
        edited = build(entry=main, flags=flags)
        assert edited[0]["functions_ir_reused"] >= 2, edited[0]
        assert edited[1:] == build(False, entry=main, flags=flags)[1:]
        assert edited[2].splitlines() == [b"stable", message.encode(), b"argument"]
