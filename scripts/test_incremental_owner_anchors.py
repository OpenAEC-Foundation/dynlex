"""Source anchors are demanded by their actual semantic owner."""


def check_owner_source_anchors(root, build):
    for flags in ((), ("-g",)):
        main = root / ("owner_anchors_debug.dl" if flags else "owner_anchors.dl")
        source = (
            'to report:\n'
            '    @intrinsic("discard", 0)\n'
            '    @intrinsic("discard", 0)\n'
            '    @intrinsic("discard", @intrinsic("call", "libc", "puts", '
            '@intrinsic("type", "int", 32), "first"))\n'
            'to unused:\n'
            '    @intrinsic("discard", 1)\n'
            'report\n'
        )
        main.write_text(source)
        build(entry=main, flags=flags)
        main.write_text(source.replace('"first"', '"second"'))
        edited = build(entry=main, flags=flags)
        assert edited[1:] == build(False, entry=main, flags=flags)[1:]
        assert edited[2] == b"second\n"
        first_work = edited[0]["body_source_anchor_lines_indexed"]
        assert first_work > 0

        # Adding unrelated body lines must not add anchor-hashing work.
        expanded = source.replace('"first"', '"third"').replace(
            '    @intrinsic("discard", 1)\n',
            '    @intrinsic("discard", 1)\n'
            '    @intrinsic("discard", 2)\n'
            '    @intrinsic("discard", 3)\n'
            '    @intrinsic("discard", 4)\n',
        )
        main.write_text(expanded)
        changed = build(entry=main, flags=flags)
        assert changed[1:] == build(False, entry=main, flags=flags)[1:]
        assert changed[2] == b"third\n"
        assert changed[0]["body_source_anchor_lines_indexed"] == first_work
        assert changed[0]["body_source_anchor_owners_indexed"] > 0

        moved = expanded.replace('to report:', '# moved duplicate lines\n\nto report:')
        moved = moved.replace('"third"', '"μ fourth"')
        main.write_text(moved)
        changed = build(entry=main, flags=flags)
        assert changed[1:] == build(False, entry=main, flags=flags)[1:]
        assert changed[2] == "μ fourth\n".encode()
