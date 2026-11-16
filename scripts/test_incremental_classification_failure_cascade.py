"""Cached signatures settle transitively even while another lookup is blocked."""


def check_classification_failure_cascade(root, build):
    provider = 'to get base item:\n    @intrinsic("return", item)\n'
    for depth in (2, 4):
        main = root / f"classification_failure_{depth}.dl"
        names = [f"hop{index}" for index in range(depth - 1)] + ["result"]
        source = provider
        previous = "base"
        for name in names:
            source += f'to get {name} item:\n    @intrinsic("return", {previous} item)\n'
            previous = name
        source += '@intrinsic("discard", result 1)\n'
        main.write_text(source)
        build(entry=main)
        removed = source.removeprefix(provider)
        unused = source.replace('"return", item)', '"return", 0)', 1)
        for text, success in (
            (removed, False),
            (removed, False),
            ("# Shift diagnostic anchors\n" + removed, False),
            (source, True),
            (unused + 'to unrelated:\n    absent call\n', False),
            (source, True),
            (unused, False),
            (source, True),
        ):
            main.write_text(text)
            incremental = build(entry=main, success=success)
            clean = build(False, entry=main, success=success)
            assert incremental[1:] == clean[1:]
            if not success:
                assert "  note:" in incremental[1], "the failed call lost its unused-parameter explanation"

        spare = 'to spare item:\n    @intrinsic("discard", item)\n'
        main.write_text(source + spare)
        build(entry=main)
        main.write_text(removed + spare)
        failed = build(entry=main, success=False)
        assert failed[1:] == build(False, entry=main, success=False)[1:]
        assert failed[0]["classification_units_reactivated"] >= depth
        assert failed[0]["classification_units_reused"] >= 1, "unrelated classification was discarded"

    main = root / "classification_named_environment.dl"
    source = '''integer means: @intrinsic("type", "int")
section answer {name}:
    declares: name
    result: integer
    value:
        42
[selected] {value} means: @intrinsic("add", value, 1)
[read] selected name means: selected name
@intrinsic("store", name, 7)
answer unrelated name:
@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", read selected name))
'''
    main.write_text(source)
    assert build(entry=main)[2] == b"8\n"
    for text, expected in ((source.replace("answer unrelated name:", "answer selected name:"), b"42\n"),
                           (source, b"8\n")):
        main.write_text(text)
        edited = build(entry=main)
        assert edited[2] == expected
        assert edited[1:] == build(False, entry=main)[1:]
