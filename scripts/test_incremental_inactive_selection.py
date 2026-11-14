"""Focused inactive-body resolution reuse cases."""


def check_literal_inactive_body(root, build):
    main = root / "inactive_literal_body.dl"
    source = ('to [the] [idle]:\n'
              '    the active\n'
              'to [the] [active]: @intrinsic("discard", 1)\n'
              'the active\n')
    main.write_text(source)
    cold = build(entry=main)
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["cached_body_resolution_references_skipped"] >= 1, \
        "an unchanged literal-only function rematched because its first word was used elsewhere"
    assert edited[0]["body_resolution_reference_visits"] < cold[0]["body_resolution_reference_visits"]
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(main.read_text().removesuffix('the active\n') + 'the idle\n')
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
