"""Unchanged body syntax stays unmaterialized until a consumer needs it."""

import json
from pathlib import Path


def syntax_work(result, *, parsed, restored, skipped):
    stats = result[0]
    assert stats["body_syntax_units_parsed"] == parsed, stats
    assert stats["body_syntax_units_restored"] == restored, stats
    assert stats["body_syntax_units_skipped"] == skipped, stats


def check_body_syntax_products(root, build):
    project = root / "body-syntax"
    project.mkdir()
    main, bodies = project / "main.dl", project / "bodies.dl"
    helper = ('function dormant value:\n    execute:\n'
              '        @intrinsic("discard", value)\n\n'
              'function sleeping value:\n    execute:\n'
              '        @intrinsic("discard", (value))\n')
    source = ('import bodies.dl\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "puts", '
              '@intrinsic("type", "int"), "before"))\n')
    main.write_text(source)
    bodies.write_text(helper)
    cold = build(entry=main)
    assert cold[2] == b"before\n"
    syntax_work(cold, parsed=2, restored=0, skipped=0)

    source = source.replace('"before"', '"after"')
    main.write_text(source)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"after\n"
    syntax_work(edited, parsed=0, restored=0, skipped=2)

    # A lexical failure must leave the prior successful unit products usable.
    bodies.write_text(helper.replace('function sleeping value:', 'function sleeping value:\x07'))
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
    assert "U+0007" in failure[1]
    bodies.write_text(helper)
    source = source.replace('"after"', '"repaired"')
    main.write_text(source)
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    syntax_work(repaired, parsed=0, restored=0, skipped=2)

    # First use demands one body without constructing the other body's syntax.
    source += 'dormant 3\n'
    main.write_text(source)
    activated = build(entry=main)
    assert activated[1:] == build(False, entry=main)[1:]
    syntax_work(activated, parsed=0, restored=1, skipped=1)

    # A new provider can retract old implicit parameters in unchanged sources.
    main.write_text(source + 'value means: 9\n')
    changed_provider = build(entry=main, success=False)
    assert changed_provider[1:] == build(False, entry=main, success=False)[1:]
    # Reclassification consumes the retained reference outline. The changed
    # call is rejected before inference needs either unchanged expression tree.
    assert changed_provider[0]["classification_units_reused"] == 0
    assert changed_provider[0]["body_resolution_reference_visits"] > 0
    syntax_work(changed_provider, parsed=1, restored=0, skipped=2)
    check_body_activation_diagnostic(root, build)
    check_partial_parser_snapshot(root, build)


def check_body_activation_diagnostic(root, build):
    main = root / "body-activation-diagnostic.dl"
    source = ('function deferred:\n    execute:\n'
              '        @intrinsic("discard", missing)\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[1:] == build(False, entry=main)[1:]
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    retained = build(entry=main)
    assert retained[1:] == build(False, entry=main)[1:]
    syntax_work(retained, parsed=0, restored=0, skipped=1)
    main.write_text(main.read_text() + 'deferred\n')
    demanded = build(entry=main, success=False)
    assert demanded[1:] == build(False, entry=main, success=False)[1:]
    assert "never assigned a value" in demanded[1]
    syntax_work(demanded, parsed=0, restored=1, skipped=0)


def check_partial_parser_snapshot(root, build):
    main = root / "partial-parser-snapshot.dl"
    source = ('function first: execute: @intrinsic("discard", 1)\n'
              'function broken: execute: @intrinsic("discard", 2)\n'
              'function later: execute: @intrinsic("discard", "retained syntax sentinel")\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 2)', '"discard", 2))'))
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
    assert "unmatched closing character" in failure[1]
    main.write_text(source.replace('"discard", 0)', '"discard", 4)'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    syntax_work(repaired, parsed=0, restored=0, skipped=3)

    # A successful complete snapshot still prunes removed source products.
    main.write_text(main.read_text().replace(source.splitlines(keepends=True)[2], ""))
    removed = build(entry=main)
    assert removed[1:] == build(False, entry=main)[1:]
    manifest_path = Path(removed[0]["cache_entry"])
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    parser = manifest_path.parent / f"parser-{manifest['parser_key']}.entry"
    assert b"retained syntax sentinel" not in parser.read_bytes()
