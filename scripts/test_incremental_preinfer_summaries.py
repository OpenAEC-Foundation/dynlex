"""Preinference captures only the interface it is comparing."""


def check_preinfer_summary_capture(root, build):
    main = root / "preinfer_summary_capture.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'flex function [emit mark]:\n'
        '    replacement:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        'function [alpha] {value}:\n    execute:\n'
        '        @intrinsic("discard", value)\n        emit mark\n'
        'function [beta]:\n    execute:\n        emit mark\n'
        'function [gamma]:\n    execute:\n        emit mark\n'
        'alpha 1\nalpha 2.0\nbeta\ngamma\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"before\n" * 4
    for message in ("after", "again"):
        main.write_text(source.replace('"before"', f'"{message}"'))
        edited = build(entry=main)
        assert edited[2] == (message + "\n").encode() * 4
        assert edited[0]["preinferred_instantiations"] == 4
        assert edited[0]["preinfer_summary_sections_captured"] == 3, \
            "preinference did not restrict summary capture to its three requested sections"
        assert edited[0]["preinfer_summary_instantiations_captured"] == 4, \
            "preinference re-encoded instantiations from earlier section groups"
        assert edited[1:] == build(False, entry=main)[1:]
