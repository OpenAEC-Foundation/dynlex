"""Infer lifecycle behavior needed by a regenerated function under a reused entry."""

from test_incremental_lifecycle_constraints import check_lifecycle_constraint_dependencies


def check_lifecycle_replacement_roots(root, build):
    check_lifecycle_pair_edits(root, build)
    check_lifecycle_constraint_dependencies(root, build)
    main = root / "lifecycle_replacement_roots.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'class:\n    patterns:\n        token\n    members:\n        value as integer\n'
        '    retain:\n        @intrinsic("discard", 1)\n'
        '    release:\n        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        'to use managed:\n'
        '    @intrinsic("store", item, @intrinsic("construct", token, 1))\n'
        '    @intrinsic("discard", @intrinsic("property", item, "value"))\n'
        '    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "managed"))\n'
        'use managed\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"managed\nbefore\n"
    for before, after, expected in (
        ('integer, "managed"', 'integer, "changed"', b"changed\nbefore\n"),
        ('integer, "before"', 'integer, "after"', b"changed\nafter\n"),
    ):
        main.write_text(main.read_text().replace(before, after))
        edited = build(entry=main)
        assert edited[0]["root_execution_reused"] and edited[0]["preinferred_instantiations"] >= 1
        assert edited[2] == expected
        assert edited[1:] == build(False, entry=main)[1:]
    valid = main.read_text()
    main.write_text(valid.replace(
        '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "after"))',
        '@intrinsic("return", 1)',
    ))
    invalid = build(entry=main, success=False)
    assert invalid[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(valid)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]


def check_lifecycle_pair_edits(root, build):
    for present, missing in (("retain", "release"), ("release", "retain")):
        main = root / f"lifecycle_missing_{missing}.dl"
        declaration = ('class:\n    patterns:\n        token\n'
                       f'    {present}:\n        @intrinsic("discard", 0)\n')
        use = ('to use managed: @intrinsic("discard", @intrinsic("construct", token))\n'
               'use managed\n')
        invalid_source = declaration + use
        main.write_text(invalid_source)
        invalid = build(entry=main, success=False)
        assert "managed class requires both retain and release sections" in invalid[1]
        assert invalid[1:] == build(False, entry=main, success=False)[1:]
        repeated = build(entry=main, success=False)
        assert repeated[0]["frontend_reused"], "unchanged malformed class repeated frontend work"
        assert repeated[1:] == invalid[1:]

        main.write_text(declaration + f'    {missing}:\n        @intrinsic("discard", 0)\n' + use)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        main.write_text(invalid_source)
        assert build(entry=main, success=False)[1:] == invalid[1:]

        # An unused declaration has the same pairing contract.
        main.write_text(declaration)
        unused = build(entry=main, success=False)
        assert unused[1:] == build(False, entry=main, success=False)[1:]
        assert "managed class requires both retain and release sections" in unused[1]
