"""Flex body edits recheck their own declaration without auditing unrelated units."""


def check_flex_declaration_retention(root, build):
    main = root / "flex_declaration_retention.dl"
    source = ('label means: "before"\n'
              'identity value means: value\n'
              'to keep value: @intrinsic("discard", value)\n'
              'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", '
              '@intrinsic("type", "int"), label))\nreport\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"before\n"

    changed = source.replace('"before"', '"after"')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"after\n", "retaining a declaration hid a changed flex expansion"
    assert edited[0]["declaration_input_units_checked"] == 1, \
        "a flex body edit audited unrelated declaration inputs"
    assert edited[0]["declaration_projection_units_built"] == 0
    assert edited[0]["declaration_projection_units_reused"] >= 4

    # Removing the final use still changes the implicit parameter's classification.
    main.write_text(changed.replace('identity value means: value', 'identity value means: 7'))
    signature = build(entry=main)
    assert signature[1:] == build(False, entry=main)[1:]
    assert signature[0]["declaration_projection_units_built"] >= 1
