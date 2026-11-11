"""Retain entry execution with managed storage and exposed global readers."""


def check_entry_metadata_reuse(root, build):
    main = root / "managed_entry.dl"
    managed = (
        'integer means: @intrinsic("type", "int")\n'
        'payload type means: @intrinsic("type", "int")\n'
        'class:\n'
        '    patterns:\n        token\n'
        '    members:\n        number\n'
        '    retain:\n        @intrinsic("return")\n'
        '    release:\n'
        '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "release"))\n'
        'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        '@intrinsic("store", item, @intrinsic("construct", token, @intrinsic("cast", 1, payload type)))\n'
        'report\n'
    )
    main.write_text(managed)
    assert build(entry=main)[2] == b"before\nrelease\n"
    changed = managed.replace('integer, "before"', 'integer, "after"')
    main.write_text(changed)
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"after\nrelease\n"
    assert edited[0]["root_execution_reused"], "managed storage disabled entry reuse after an unrelated body edit"
    main.write_text(changed.replace('integer, "release"', 'integer, "released"'))
    lifecycle = build(entry=main)
    assert lifecycle[1:] == build(False, entry=main)[1:]
    assert lifecycle[2] == b"after\nreleased\n"
    assert not lifecycle[0]["root_execution_reused"], "changed cleanup body retained its caller entry"
    main.write_text(main.read_text().replace(
        'payload type means: @intrinsic("type", "int")',
        'payload type means: @intrinsic("type", "float")',
    ))
    layout = build(entry=main)
    assert not layout[0]["root_execution_reused"], "changed field type provider retained an old entry layout"
    assert layout[1:] == build(False, entry=main)[1:]

    main = root / "exposed_entry.dl"
    exposed = (
        'integer means: @intrinsic("type", "int")\n'
        'exposed function augment {integer:value}:\n'
        '    globals:\n        count\n'
        '    execute:\n        @intrinsic("return", @intrinsic("add", value, count))\n'
        'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
        '@intrinsic("store", count, @intrinsic("command line argument count"))\n'
        'report\n'
        '@intrinsic("return", @intrinsic("subtract", augment 1, 1))\n'
    )
    main.write_text(exposed)
    assert build(entry=main)[2] == b"before\n"
    main.write_text(exposed.replace('integer, "before"', 'integer, "after"'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[2] == b"after\n"
    assert edited[0]["root_execution_reused"], "exposed global reader disabled entry reuse after an unrelated body edit"
    main.write_text(exposed.replace('integer, "before"', 'integer, "again"'))
    repeated = build(entry=main)
    assert repeated[0]["root_execution_reused"], "entry replay discarded the exposed function's global type state"
    assert repeated[1:] == build(False, entry=main)[1:]
    assert repeated[2] == b"again\n"

    main = root / "exposed_global_diagnostic.dl"
    diagnostic = (
        'integer means: @intrinsic("type", "int")\n'
        'exposed function alter {integer:value}:\n'
        '    globals:\n        count\n'
        '    execute:\n        @intrinsic("store", count, value)\n'
        '@intrinsic("store", count, 1)\n'
    )
    main.write_text(diagnostic)
    build(entry=main)
    main.write_text(diagnostic.replace(
        '"store", count, value',
        '"store", count, @intrinsic("cast", value, @intrinsic("type", "float"))',
    ))
    error = build(entry=main, success=False)
    assert error[0]["root_execution_reused"], "uncalled exposed body invalidated the entry"
    assert error[1:] == build(False, entry=main, success=False)[1:], "restored global lost its type diagnostic origin"
    main.write_text(diagnostic)
    recovered = build(entry=main)
    assert recovered[0]["frontend_reused"], "exposed inference error discarded the preceding successful native result"
    assert recovered[1:] == build(False, entry=main)[1:]
