"""Matcher reuse depends on syntax and bindings, independently of flex execution."""


def check_choice_declaration_replay(root, build):
    main = root / "choice_main.dl"
    output = root / "choice_program"
    main.write_text('to [alpha|beta]: @intrinsic("discard", 1)\nalpha\n')
    build(entry=main, target=output)
    main.write_text('to [alpha|beta]: @intrinsic("discard", 1)\nbeta\n')
    edited = build(entry=main, target=output)
    assert edited[0]["definition_parse_hits"] > 0, "choice declaration was reparsed after a call edit"
    assert edited[1:] == build(False, entry=main, target=output)[1:]


def check_deferred_body_match_replay(root, build):
    main = root / "deferred_body_match_replay.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'section using {integer:id}:\n'
              '    receiver: integer\n'
              '    returns: integer\n'
              '    value: @intrinsic("discard", @intrinsic("invoke", @intrinsic("deferred body"), 0))\n'
              'to test:\n'
              '    using 1:\n'
              '        @intrinsic("return", @intrinsic("receiver"))\n'
              'to keep: test\n'
              'to touch: @intrinsic("discard", 1)\n'
              'keep\ntouch\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 1)', '"discard", 2)'))
    edited = build(entry=main)
    assert edited[0]["deferred_match_keys_replayed"] >= 1, \
        "generated receiver functions invalidated the earlier matcher stage"
    assert edited[1:] == build(False, entry=main)[1:]


def check_flex_body_match_replay(root, build):
    main = root / "flex_body_match_replay.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'the bonus means: 1\n'
              'function compute {integer:item}:\n'
              '    execute:\n'
              '        @intrinsic("return", @intrinsic("add", item, the bonus))\n'
              '@intrinsic("store", input, @intrinsic("call", "libc", "rand", integer))\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "printf", integer, "%d\\n", '
              '@intrinsic("subtract", compute input, input)))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"1\n"
    main.write_text(source.replace('the bonus means: 1', 'the bonus means: 2'))
    edited = build(entry=main)
    assert edited[0]["deferred_match_keys_replayed"] >= 1, \
        "a flex body edit invalidated unchanged function matcher keys"
    assert edited[2] == b"2\n", "saved inference or IR retained an old flex result"
    assert edited[1:] == build(False, entry=main)[1:]

    # A runtime caller may also embed a known result from the same instantiation.
    folded = source.replace('@intrinsic("store", input,',
                            'function report {integer:item}:\n'
                            '    execute:\n'
                            '        @intrinsic("return", @intrinsic("add", item, compute 0))\n'
                            '@intrinsic("store", input,')
    folded = folded.replace('@intrinsic("subtract", compute input, input)',
                            '@intrinsic("subtract", report input, input)')
    folded = folded.replace('@intrinsic("discard", @intrinsic("call", "libc", "printf",',
                            '@intrinsic("discard", compute input)\n'
                            '@intrinsic("discard", @intrinsic("call", "libc", "printf",')
    main.write_text(folded)
    assert build(entry=main)[2] == b"1\n"
    main.write_text(folded.replace('the bonus means: 1', 'the bonus means: 2'))
    changed_fold = build(entry=main)
    assert changed_fold[2] == b"2\n", "a stable type interface preserved a stale folded call"
    assert changed_fold[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text() + '@intrinsic("discard", compute 0)\n')
    reused_value = build(entry=main)
    assert reused_value[0]["pure_value_summary_hits"] >= 1, \
        "an unchanged dependent pure function lost its observed constant result"
    assert reused_value[1:] == build(False, entry=main)[1:]

    # Body-driven parameter classification still changes the syntax seen by callers.
    source = ('integer means: @intrinsic("type", "int")\n'
              'use thing means: @intrinsic("add", thing, 1)\n'
              'function compute {integer:item}:\n'
              '    execute:\n'
              '        @intrinsic("return", use item)\n'
              '@intrinsic("discard", compute 2)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"add", thing, 1', '"add", 1, 1'))
    changed_signature = build(entry=main, success=False)
    assert changed_signature[1:] == build(False, entry=main, success=False)[1:], \
        "flex parameter removal replayed a stale caller match"
