"""Native lowering runs only for changed, reachable optimized units."""

import os
from unittest.mock import patch

from test_incremental_program_cache import read_program


def check_optimized_native_codegen(root, build):
    main = root / "optimized_native_dependencies.dl"
    source = ('integer means: @intrinsic("type", "int")\n' +
              ''.join(f'exposed function callback{index}:\n    execute:\n'
                      '        @intrinsic("discard", @intrinsic("call", "libc", "puts", '
                      f'integer, "callback{index}"))\n' for index in range(3)) +
              ''.join('@intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
                      f'@intrinsic("function", "callback{index}")))\n' for index in range(3)))
    flags = ("-O2",)
    target = main.with_suffix(".exe" if os.name == "nt" else ".out")
    temporary = root / ("object files" if os.name == "nt" else 'object files "quoted" \\')
    temporary.mkdir()

    def compile_native(incremental=True):
        with patch.dict(os.environ, {key: str(temporary) for key in ("TMPDIR", "TEMP", "TMP")}):
            return build(incremental, entry=main, flags=flags, target=target)

    main.write_text(source)
    cold = compile_native()
    assert cold[0]["native_functions"] >= 3, cold[0]
    assert cold[0]["native_functions_lowered"] == cold[0]["native_functions"], cold[0]
    assert cold[2].splitlines() == [b"callback2", b"callback1", b"callback0"]
    for text in ("edited", "edited again"):
        main.write_text(source.replace('integer, "callback1"', f'integer, "{text}"'))
        edited = compile_native()
        assert edited[0]["native_functions_lowered"] == 1, edited[0]
        assert edited[0]["objects_reused"] >= 2, edited[0]
        assert edited[1:] == compile_native(False)[1:]
        assert edited[2].splitlines() == [b"callback2", text.encode(), b"callback0"]

    # Rebinding and liveness can change independently of an unchanged callback.
    added = ('exposed function another callback:\n    execute:\n'
             '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "new"))\n' +
             source + '@intrinsic("discard", @intrinsic("call", "libc", "atexit", integer, '
             '@intrinsic("function", "another callback")))\n')
    main.write_text(added)
    inserted = compile_native()
    assert inserted[0]["native_functions_lowered"] == 2, inserted[0]
    assert inserted[1:] == compile_native(False)[1:]
    main.write_text(source)
    removed = compile_native()
    assert removed[0]["native_functions_lowered"] == 0, removed[0]
    assert removed[1:] == compile_native(False)[1:]

    # Losing an object should require native lowering of that unit only.
    cache = root / "cache"
    object_key = read_program(cache, main)["object_keys"][0]
    (cache / "native-v1" / f"object-{object_key}.entry").unlink()
    target.unlink()
    regenerated = compile_native()
    assert regenerated[0]["frontend_reused"] and regenerated[0]["objects_built"] == 1, regenerated[0]
    assert regenerated[0]["optimization_units_built"] == 0, regenerated[0]
    assert regenerated[1:] == removed[1:]

    # If the unit's optimized IR is also gone, request that optimizer unit only.
    snapshot = read_program(cache, main)
    unit = next(iter(snapshot["native_units"]["units"].values()))
    assert unit["bitcode_key"] not in snapshot["payload_keys"].values()
    (cache / "native-v1" / f"object-{unit['object_key']}.entry").unlink()
    (cache / "native-v1" / f"payload-{unit['bitcode_key']}.entry").unlink()
    target.unlink()
    rebuilt = compile_native()
    assert rebuilt[0]["frontend_reused"] and rebuilt[0]["objects_built"] == 1, rebuilt[0]
    assert rebuilt[0]["optimization_units_built"] == 1, rebuilt[0]
    assert rebuilt[1:] == removed[1:]
