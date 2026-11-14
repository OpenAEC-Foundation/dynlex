"""A changed native unit copies its dependencies rather than unrelated globals."""


def check_partition_closure_work(root, build):
    main = root / "partition_closure_work.dl"
    source = ('integer means: @intrinsic("type", "int")\n' +
              ''.join(f'to part{index}: @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                      f'integer, "part{index}"))\n' for index in range(2)) +
              ''.join(f'part{index}\n' for index in range(2)))
    for optimization in ("-O0", "-O2"):
        main.write_text(source)
        cold = build(entry=main, flags=(optimization,))
        main.write_text(source.replace('"part1"', '"changed"'))
        edited = build(entry=main, flags=(optimization,))
        assert edited[1:] == build(False, entry=main, flags=(optimization,))[1:]
        assert edited[2].splitlines()[1] == b"changed"
        assert edited[0]["native_functions_lowered"] == 1
        # Optimized builds construct ThinLTO inputs; O0 builds construct native
        # partitions directly. The other builder does no work on that path.
        stage = "native_partition" if optimization == "-O0" else "optimization_input"
        assert edited[0][stage + "_declarations_cloned"] <= 4, edited[0]
        assert edited[0][stage + "_definitions_cloned"] <= 4, edited[0]
        assert cold[0][stage + "_declarations_cloned"] > edited[0][stage + "_declarations_cloned"]
