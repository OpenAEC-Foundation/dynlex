"""Entry replay preserves debug global order independently of IR symbol sorting."""

import os


def check_debug_entry_globals(root, build):
    for structured in (False, True):
        main = root / ("debug_entry_class.dl" if structured else "debug_entry_scalar.dl")
        source = (
            '@intrinsic("store", zebra, @intrinsic("command line argument count"))\n'
            '@intrinsic("store", shared, @intrinsic("command line argument count"))\n'
            'function show:\n    globals: shared\n    execute:\n'
            '        @intrinsic("variadic call", "libc", "printf", '
            '@intrinsic("type", "nothing"), 1, "%d\\n", @intrinsic("add", shared, 1))\n'
            'show\n'
            'function observe:\n    globals: zebra\n'
            '    execute: @intrinsic("discard", zebra)\n'
            'observe\n'
        )
        if structured:
            source = ('class:\n    patterns:\n        record\n    members:\n        number\n' +
                      source.replace('@intrinsic("store", shared, @intrinsic("command line argument count"))',
                                     '@intrinsic("store", shared, @intrinsic("construct", record, '
                                     '@intrinsic("command line argument count")))')
                            .replace('"add", shared, 1', '"add", shared\'s number, 1'))
        target = root / ("entry_debug.exe" if os.name == "nt" else "entry_debug.out")
        main.write_text(source)
        build(entry=main, flags=("-g",), target=target)
        for value in (2, 3):
            main.write_text(source.replace(', 1))', f', {value}))'))
            edited = build(entry=main, flags=("-g",), target=target)
            image = target.read_bytes()
            clean_cache = root / f"debug_entry_clean_{structured}_{value}"
            assert not clean_cache.exists()
            clean = build(entry=main, flags=("-g",), target=target, cache_root=clean_cache)
            assert edited[1:] == clean[1:]
            assert image == target.read_bytes(), "entry replay changed native debug output"
            assert edited[0]["root_execution_reused"]
            assert not clean[0]["root_execution_reused"] and not clean[0]["functions_ir_reused"]
