"""Runtime lifecycle dependencies must not invalidate unrelated class layouts."""


def check_lifecycle_constraint_dependencies(root, build):
    for flex in (False, True):
        main = root / f"lifecycle_constraint_{flex}.dl"
        header = 'flex function [trace]:\n    replacement:\n' if flex else 'function [trace]:\n    execute:\n'
        source = (
            'integer means: @intrinsic("type", "int")\n' + header +
            '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
            'class:\n    patterns:\n        token\n    members:\n        value as integer\n'
            '    retain:\n        trace\n'
            '    release:\n        trace\n'
            'to consume {token:item}: @intrinsic("discard", @intrinsic("property", item, "value"))\n'
            'consume (@intrinsic("construct", token, 1))\n'
        )
        main.write_text(source)
        assert build(entry=main)[2] == b"before\nbefore\n"
        main.write_text(source.replace('"before"', '"after"'))
        edited = build(entry=main)
        assert edited[2] == b"after\nafter\n", "a cached lifecycle kept an old helper body"
        assert edited[0]["constraint_probes_reused"] >= 1
        assert edited[0]["constraint_probes_run"] == 0, "a runtime lifecycle helper invalidated the class type"
        assert edited[1:] == build(False, entry=main)[1:]
        valid = main.read_text()
        main.write_text(valid.replace(
            '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "after"))',
            '@intrinsic("add", 1)',
        ))
        invalid = build(entry=main, success=False)
        assert invalid[1:] == build(False, entry=main, success=False)[1:], \
            "a cached entry hid a lifecycle helper's type error"
        main.write_text(valid)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        main.write_text(main.read_text().replace('    retain:\n        trace\n', '')
                        .replace('    release:\n        trace\n', ''))
        removed = build(entry=main)
        assert removed[2] == b"", "removing lifecycle hooks retained their calls"
        assert removed[1:] == build(False, entry=main)[1:]

    main = root / "lifecycle_member_constraint.dl"
    source = ('field kind means: @intrinsic("type", "int", 32)\n'
              'to scalar {integer:number}: @intrinsic("discard", number)\n' +
              source.replace('value as integer', 'value as field kind') +
              'scalar 1\n'
              '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
              '@intrinsic("cast", @intrinsic("size of", token), integer)))\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"before\nbefore\n4\n"
    main.write_text(source.replace('field kind means: @intrinsic("type", "int", 32)',
                                   'field kind means: @intrinsic("type", "int", 64)'))
    changed_layout = build(entry=main)
    assert changed_layout[0]["constraint_probes_run"] >= 1, "a changed member type reused the old class layout"
    assert changed_layout[2] == b"before\nbefore\n8\n", "a changed member type retained the old size"
    assert changed_layout[1:] == build(False, entry=main)[1:]
