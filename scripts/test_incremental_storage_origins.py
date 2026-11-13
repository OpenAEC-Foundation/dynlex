"""Preserve lexical pointer origins when replaying inference summaries."""

from test_incremental_global_inputs import check_global_type_inputs


def check_storage_origin_reuse(root, build):
    check_global_type_inputs(root, build)
    declarations = (
        'integer means: @intrinsic("type", "int")\n'
        'function origin {integer:number}:\n'
        '    execute:\n'
        '        @intrinsic("store", result, number)\n'
        '        @intrinsic("return", @intrinsic("address of", result))\n'
    )
    macro = (
        'function local address for {integer:value}:\n'
        '    replacement:\n'
        '        @intrinsic("store", hidden, value)\n'
        '        @intrinsic("address of", hidden)\n'
        'function nested address for {integer:value}:\n'
        '    replacement: local address for value\n'
    )
    sources = {
        "direct": declarations,
        "callee": declarations + (
            'function wrapper:\n'
            '    execute:\n'
            '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "wrapped"))\n'
            '        @intrinsic("return", origin @intrinsic("command line argument count"))\n'
        ),
        "macro": declarations.replace(
            '@intrinsic("return", @intrinsic("address of", result))',
            '@intrinsic("return", nested address for result)',
        ) + macro,
    }
    for name, definitions in sources.items():
        main = root / f"storage_origins_{name}.dl"
        call = "wrapper" if name == "callee" else 'origin @intrinsic("command line argument count")'
        source = definitions + f'@intrinsic("discard", {call})\n'
        main.write_text(source)
        build(entry=main)
        main.write_text(source + '@intrinsic("discard", 0)\n')
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[0]["inference_instantiations_reused"] >= 1, f"{name} return origins prevented inference reuse"
        commented = (source + '@intrinsic("discard", 0)\n').replace(
            '    execute:\n', '    execute:\n        # Keep invocation identities across comment edits.\n'
        )
        main.write_text(commented)
        comment = build(entry=main)
        assert comment[0]["frontend_reused"]
        main.write_text(commented + '@intrinsic("discard", 1)\n')
        after_comment = build(entry=main)
        assert after_comment[0]["inference_instantiations_reused"] >= 1
        assert after_comment[1:] == build(False, entry=main)[1:]
        main.write_text(definitions + f'@intrinsic("check deallocation", {call})\n')
        diagnosed = build(success=False, entry=main)
        assert diagnosed[1:] == build(False, success=False, entry=main)[1:]
        assert "deallocat" in diagnosed[1]
        null = '@intrinsic("construct", @intrinsic("add pointer depth", integer))'
        changed = definitions.replace(
            'nested address for result' if name == "macro" else '@intrinsic("address of", result)', null
        )
        main.write_text(changed + f'@intrinsic("check deallocation", {call})\n')
        assert build(entry=main)[1:] == build(False, entry=main)[1:]

    main = root / "storage_origins_global.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function publish {integer:number}:\n'
        '    globals: pointer\n'
        '    execute:\n'
        '        @intrinsic("store", local, number)\n'
        '        @intrinsic("store", pointer, @intrinsic("address of", local))\n'
        '@intrinsic("store", pointer, @intrinsic("construct", @intrinsic("add pointer depth", integer)))\n'
        'publish @intrinsic("command line argument count")\n'
    )
    main.write_text(source)
    build(entry=main)
    main.write_text(source + '@intrinsic("discard", 0)\n')
    edited = build(entry=main)
    assert edited[0]["inference_instantiations_reused"] >= 1, "scoped global write prevented inference reuse"
    assert edited[1:] == build(False, entry=main)[1:]
    main.write_text(source + '@intrinsic("check deallocation", pointer)\n')
    diagnosed = build(success=False, entry=main)
    assert diagnosed[1:] == build(False, success=False, entry=main)[1:]
    assert "deallocat" in diagnosed[1]

    main = root / "storage_origins_parameter.dl"
    parameter = declarations.replace('@intrinsic("address of", result)', '@intrinsic("address of", number)')
    source = parameter + (
        '@intrinsic("discard", origin @intrinsic("command line argument count"))\n'
        '@intrinsic("check deallocation", origin @intrinsic("command line argument count"))\n'
    )
    main.write_text(source)
    build(entry=main)
    main.write_text(source + '@intrinsic("discard", 0)\n')
    edited = build(entry=main)
    assert edited[0]["inference_instantiations_reused"] >= 1
    assert edited[1:] == build(False, entry=main)[1:]

    main = root / "storage_origins_body_edit.dl"
    before = '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n'
    after = '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "after"))\n'
    source = sources["macro"].replace('    execute:\n', '    execute:\n' + before + after)
    source += '@intrinsic("discard", origin @intrinsic("command line argument count"))\n'
    main.write_text(source)
    assert build(entry=main)[2] == b"before\nafter\n"
    main.write_text(source.replace(before, ""))
    edited = build(entry=main)
    assert edited[0]["root_execution_reused"], "moving an unchanged macro invocation invalidated its caller"
    assert edited[0]["preinferred_instantiations"] >= 1
    assert edited[2] == b"after\n"
    assert edited[1:] == build(False, entry=main)[1:]

