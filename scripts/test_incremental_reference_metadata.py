"""Resolution stages share immutable metadata without retaining stale matches."""


def check_reference_metadata_reuse(root, build):
    main = root / "reference_metadata.dl"
    source = '''integer means: @intrinsic("type", "int")
to first: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))
to caller: first
caller
'''
    main.write_text(source)
    initial = build(entry=main)
    assert initial[2] == b"first\n"
    assert initial[0]["reference_metadata_units_loaded"] == 0
    for text in ("edited", "edited again"):
        main.write_text(source.replace('integer, "first"', f'integer, "{text}"'))
        edited = build(entry=main)
        assert edited[2] == text.encode() + b"\n"
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["reference_metadata_units_loaded"] > 0
        assert edited[0]["reference_metadata_units_reused"] > 0, \
            "classification and replay decoded the same immutable reference metadata again"
    added = 'to unrelated: @intrinsic("discard", 5)\n' + main.read_text()
    main.write_text(added)
    inserted = build(entry=main)
    assert inserted[1:] == build(False, entry=main)[1:]
    assert inserted[0]["reference_metadata_units_reused"] > 0
    main.write_text(added.replace('to first:', 'to removed:'))
    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:], \
        "shared reference metadata hid removal of a previously matched pattern"
