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
    assert stats["inference_query_proofs_invalidated"] <= stats["inference_query_units_checked"]
    skipped = build(entry=main)
    assert skipped[1:] == edited[1:]
    assert all(skipped[0][key] == 0 for key in (
        "inference_query_checks", "inference_query_units_checked", "inference_query_proof_hits",
        "inference_query_proofs_invalidated", "inference_query_check_ms"))
