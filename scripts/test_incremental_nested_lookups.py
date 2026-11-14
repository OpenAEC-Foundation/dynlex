"""Section headers depend on the function patterns matched inside their captures."""

from test_incremental_program_cache import read_program


def check_nested_classification_lookups(root, build):
    source = (
        'flex section [check] {condition}:\n'
        '    replacement:\n'
        '        @intrinsic("discard", condition)\n'
        '        @intrinsic("execute body")\n'
        'function [outer] value:\n'
        '    execute:\n'
        '        check value:\n'
        '            @intrinsic("discard", 0)\n'
        '@intrinsic("discard", outer 7)\n'
    )
    for mode in ("global", "local", "imported", "private_import"):
        main = root / f"nested_lookup_{mode}.dl"
        helper = root / f"nested_lookup_{mode}_provider.dl"
        imported = mode in ("imported", "private_import")
        prefix = f"import {helper.name}\n" if imported else ""
        if imported:
            helper.write_text("")
        main.write_text(prefix + source)
        assert build(entry=main)[1:] == build(False, entry=main)[1:]
        provider = ('local function [value]:\n    replacement:\n        9\n'
                    if mode in ("local", "private_import") else 'value means: 9\n')
        if imported:
            helper.write_text(provider)
        else:
            main.write_text(prefix + source + provider)
        if mode == "private_import":
            invisible = build(entry=main)
            assert invisible[0]["classification_parameters_reused"] >= 1
            assert invisible[1:] == build(False, entry=main)[1:]
            continue
        affected = build(entry=main, success=False)
        assert affected[0]["classification_units_invalidated"] >= 1
        assert affected[1:] == build(False, entry=main, success=False)[1:], \
            "a nested function lookup retained an implicit parameter"
        main.write_text(main.read_text().replace('outer 7)', 'outer value)'))
        recovered = build(entry=main)
        assert recovered[1:] == build(False, entry=main)[1:]
        declarations = read_program(root / "cache", main)["all_function_projection"]["function_declarations"]
        assert all(not element[4] for declaration in declarations.values()
                   for definition in declaration["definitions"] for element in definition["elements"]
                   if element[1] == "value"), "the parameter did not become a literal word"
        if imported:
            helper.write_text("")
        main.write_text(prefix + source)
        removed = build(entry=main)
        assert removed[1:] == build(False, entry=main)[1:], "removing a nested provider did not restore the parameter"
