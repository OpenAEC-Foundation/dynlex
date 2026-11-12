"""Cached semantic facts and native IR do not require source-body clones."""


def check_body_materialization(root, build):
    main = root / 'body_materialization.dl'
    source = ('to emit {value}:\n'
              '    @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", value)\n'
              'emit @intrinsic("command line argument count")\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b'0\n'
    assert cold[0]['source_function_body_clones'] > 0
    for value in (2, 3):
        main.write_text(source.replace('"discard", 1', f'"discard", {value}'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]['function_body_inference_passes'] == 0
        assert edited[0]['source_function_body_clones'] == 0, \
            'cached facts/IR cloned the unchanged source body'
    main.write_text(source.replace('"%d\\n"', '"value=%d\\n"'))
    changed = build(entry=main)
    assert changed[2] == b'value=0\n'
    assert changed[1:] == build(False, entry=main)[1:]
    assert changed[0]['function_body_inference_passes'] == 1
    assert changed[0]['source_function_body_clones'] == 1

    # A new argument needs the saved typed body, not a fresh source template.
    main = root / 'body_materialization_pure.dl'
    source = ('to get next {value}:\n'
              '    @intrinsic("return", @intrinsic("add", value, 1))\n'
              '@intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", next 1)\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    assert build(entry=main)[2] == b'2\n'
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    observed = build(entry=main)
    assert observed[1:] == build(False, entry=main)[1:]
    assert observed[0]['source_function_body_clones'] == 0
    assert observed[0]['function_body_inference_passes'] == 0
    assert observed[0]['inference_bodies_loaded'] == 0
    for value in (2, 3):
        main.write_text(source.replace('next 1)', f'next {value})'))
        edited = build(entry=main)
        assert edited[2] == f'{value + 1}\n'.encode()
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]['function_body_inference_passes'] == 0
        assert edited[0]['inference_bodies_loaded'] > 0
        assert edited[0]['source_function_body_clones'] == 0
