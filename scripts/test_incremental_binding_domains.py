"""Demanding one binding domain does not scan other functions' references."""


def check_binding_domain_index(root, build):
    main = root / "binding_domain_index.dl"
    source = ""
    for name, value in (("alpha", 0), ("beta", 1), ("gamma", 2)):
        source += (f'to get {name} {{seed}}:\n'
                   f'    @intrinsic("store", local, @intrinsic("add", seed, {value}))\n'
                   '    @intrinsic("return", local)\n')
    source += ('@intrinsic("variadic call", "libc", "printf", '
               '@intrinsic("type", "nothing"), 1, "%d %d %d\\n", '
               'alpha @intrinsic("command line argument count"), '
               'beta @intrinsic("command line argument count"), '
               'gamma @intrinsic("command line argument count"))\n'
               '@intrinsic("discard", 0)\n')
    main.write_text(source)
    cold = build(entry=main, flags=("-g",))
    assert cold[2] == b"0 1 2\n"
    for value in (1, 2):
        main.write_text(source.replace('"discard", 0', f'"discard", {value}'))
        edited = build(entry=main, flags=("-g",))
        assert edited[1:] == build(False, entry=main, flags=("-g",))[1:]
        work = edited[0]
        assert work["binding_domain_selections"] >= 3, work
        assert work["binding_domain_index_builds"] == 1, work
        assert work["binding_reference_visits"] <= cold[0]["binding_reference_visits"], work
        assert work["binding_domain_index_reference_visits"] <= cold[0]["binding_reference_visits"], work
        assert work["binding_units_reused"] >= 3, work
