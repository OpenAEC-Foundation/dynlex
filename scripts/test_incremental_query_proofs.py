"""Count dependency-query proof work and preserve clean-build behavior."""


def check_query_proof_accounting(root, build):
    main = root / "query_proofs.dl"
    source = ('to get leaf {item}:\n'
              '    @intrinsic("return", @intrinsic("add", item, 1))\n'
              'to get outer {item}:\n'
              '    @intrinsic("return", leaf item)\n'
              'to get side {item}:\n'
              '    @intrinsic("return", @intrinsic("multiply", item, 3))\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), 1, "%d\\n", '
              '@intrinsic("add", outer @intrinsic("command line argument count"), '
              'side @intrinsic("command line argument count")))\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('item, 1', 'item, 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    stats = edited[0]
    assert stats["inference_query_checks"] > 0
    assert stats["inference_query_units_checked"] > 0
    assert stats["inference_query_check_ms"] > 0
    assert stats["inference_query_proofs_retained"] > 0, "an unrelated positive proof was discarded"
    assert stats["inference_query_proofs_invalidated"] <= stats["inference_query_units_checked"]
    skipped = build(entry=main)
    assert skipped[1:] == edited[1:]
    assert all(skipped[0][key] == 0 for key in (
        "inference_query_checks", "inference_query_units_checked", "inference_query_proof_hits",
        "inference_query_proofs_invalidated", "inference_query_proofs_retained",
        "inference_query_consumer_edges_visited", "inference_query_invalidation_ms", "inference_query_check_ms"))
    check_query_proof_cascades(root, build)


def check_query_proof_cascades(root, build):
    prelude = ('integer means: @intrinsic("type", "int")\n'
               'section if condition:\n'
               '    replacement: @intrinsic("if", condition)\n')
    main = root / "query_proof_cycle.dl"
    source = (prelude + 'to first {integer:depth}:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
              '    if @intrinsic("greater than", depth, 0):\n'
              '        second @intrinsic("subtract", depth, 1)\n'
              'to second {integer:depth}:\n'
              '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "second"))\n'
              '    if @intrinsic("greater than", depth, 0):\n'
              '        first @intrinsic("subtract", depth, 1)\n'
              'to unrelated: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
              'first @intrinsic("add", @intrinsic("command line argument count"), 1)\nunrelated\n')
    main.write_text(source)
    build(entry=main)
    changed = source.replace('"before"', '"after"')
    main.write_text(changed)
    retained = build(entry=main)
    assert retained[1:] == build(False, entry=main)[1:]
    assert retained[2] == b"first\nsecond\nafter\n"
    assert retained[0]["inference_query_proofs_retained"] > 0
    changed = changed.replace('integer, "first"', 'integer, "updated first"')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"updated first\nsecond\nafter\n"
    main.write_text(changed.replace('to second ', 'to missing '))
    assert build(entry=main, success=False)[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(changed)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "query_proof_value.dl"
    source = (prelude + 'to get fixed value:\n    @intrinsic("return", 5)\n'
              'to get choose {item}:\n'
              '    if @intrinsic("equal", fixed value, 5):\n'
              '        @intrinsic("return", item)\n'
              '    @intrinsic("return", @intrinsic("add", item, 10))\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
              'choose @intrinsic("command line argument count")))\n')
    main.write_text(source)
    build(entry=main)
    for value in (6, 7, 5):
        main.write_text(source.replace('"return", 5', f'"return", {value}'))
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == (b"0\n" if value == 5 else b"10\n")
