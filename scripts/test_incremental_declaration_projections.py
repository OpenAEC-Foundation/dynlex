"""Resolved declaration projections follow semantic changes, not body text."""

from test_incremental_program_cache import read_program


def check_declaration_projection_units(root, build):
    main = root / "declaration_projection_units.dl"
    source = ('to keep value: @intrinsic("discard", value)\n'
              'to touch: @intrinsic("discard", 0)\ntouch\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[0]["declaration_projection_units_built"] >= 2
    assert cold[0]["declaration_records_built"] >= 2
    before = read_program(root / "cache", main)["all_function_projection"]

    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    body = build(entry=main)
    assert body[1:] == build(False, entry=main)[1:]
    assert body[0]["declaration_projection_units_built"] == 0
    assert body[0]["declaration_projection_units_reused"] >= 2
    assert body[0]["declaration_records_built"] == 0
    assert body[0]["declaration_input_units_checked"] == 1, \
        "a body edit audited an unchanged declaration input"

    main.write_text(main.read_text().replace('"discard", value', '"discard", 0'))
    signature = build(entry=main)
    assert signature[1:] == build(False, entry=main)[1:]
    assert signature[0]["declaration_projection_units_built"] == 1
    assert signature[0]["declaration_projection_units_reused"] >= 1
    after = read_program(root / "cache", main)["all_function_projection"]
    assert after["declarations"] != before["declarations"]
    assert "pattern_declarations" not in after
    assert after["declaration_fingerprints"]
    check_failed_declaration_projection_units(root, build)
    check_declaration_syntax_profile(root, build)


def check_failed_declaration_projection_units(root, build):
    main = root / "failed_declaration_projection_units.dl"
    source = ('to broken {value}:\n'
              '    @intrinsic("discard", @intrinsic("multiply", value, "text"))\n'
              'broken 1\n')
    main.write_text(source)
    build(entry=main, success=False)
    main.write_text(source.replace('broken 1', 'broken 2'))
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    assert edited[0]["declaration_projection_units_built"] == 0
    assert edited[0]["declaration_projection_units_reused"] >= 1


def check_declaration_syntax_profile(root, build):
    folder = root / "declaration_syntax_profile"
    folder.mkdir()
    main, config = folder / "main.dl", folder / "config.dl"
    main.write_text('function stable:\n    execute:\n        @intrinsic("discard", 0)\nstable\n')
    config.write_text('dynlex options:\n    shorthand definitions:\n        action: "to"\n')
    build(entry=main)
    config.write_text('dynlex options:\n    shorthand definitions:\n        action: "perform"\n')
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["declaration_input_units_checked"] >= 1, \
        "changed syntax reused a source-only declaration proof"
    assert edited[0]["declaration_projection_units_reused"] >= 1
    assert edited[0]["declaration_records_built"] == 0
