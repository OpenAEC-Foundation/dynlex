"""Native emission visits the changed symbol partitions and their interfaces."""

import json
import os
import hashlib
from test_incremental_program_cache import read_program


def check_interned_literal_identity(root, build):
    main = root / "interned_literal.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "shared"))\n'
              'to touch: @intrinsic("discard", 1)\n'
              'report\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "shared"))\n'
              'touch\n')
    main.write_text(source)
    cold = build(entry=main)
    marker = ".dynlex.string." + hashlib.sha256(b"shared").hexdigest()

    def literal_symbols():
        return {name for name in read_program(root / "cache", main)["native_units"]["symbols"]
                if marker in name}

    symbols = literal_symbols()
    assert len(symbols) == 1, "a clean module did not intern identical string literals"
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[0]["functions_ir_reused"] >= 1
    assert literal_symbols() == symbols, "cached IR import duplicated an interned literal"
    assert cold[1:] == edited[1:] == build(False, entry=main)[1:]


def check_native_partition_edits(root, build):
    main = root / "partition_main.dl"
    main_source = ('integer means: @intrinsic("type", "int")\n' +
                   ''.join(f'import part{i:02d}.dl\n' for i in range(20)) +
                   ''.join(f'part{i:02d}\n' for i in range(20)))
    main.write_text(main_source)
    for index in range(20):
        (root / f"part{index:02d}.dl").write_text(
            f"to part{index:02d}:\n" +
            ''.join('    @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, '
                    f'"part{index:02d}-{line:02d}"))\n' for line in range(5)))
    build(entry=main)
    helper = root / "part07.dl"
    helper.write_text(helper.read_text().replace('"part07-00"', '"changed"'))
    edited = build(entry=main)
    assert edited[0]["inference_instantiations_reused"] >= 19
    assert edited[0]["functions_ir_reused"] == 19
    assert edited[0]["native_functions_lowered"] == 1, "unrelated functions shared the edited function's native unit"
    assert edited[0]["objects_built"] <= 3
    assert edited[0]["native_function_dependencies_reused"] >= 19
    assert edited[0]["native_partition_modules_skipped"] >= 1, "unchanged native units were cloned again"
    assert edited[0]["native_partition_modules_built"] <= 4
    assert edited[1:] == build(False, entry=main)[1:]
    for index in (8, 9):
        path = root / f"part{index:02d}.dl"
        path.write_text(path.read_text().replace(f'"part{index:02d}-00"', f'"updated-{index}"'))
    multiple = build(entry=main)
    assert multiple[0]["inference_instantiations_reused"] == 18
    assert multiple[0]["functions_ir_reused"] == 18
    assert multiple[0]["native_partition_modules_skipped"] >= 1
    assert multiple[1:] == build(False, entry=main)[1:]

    helper_source = helper.read_text()
    helper.write_text(helper_source + 'to extra: @intrinsic("discard", 42)\n')
    main.write_text(main_source + 'extra\n')
    added = build(entry=main)
    assert added[0]["native_partition_modules_skipped"] >= 1, "an unrelated declaration invalidated every object"
    assert added[1:] == build(False, entry=main)[1:]
    helper.write_text(helper_source)
    main.write_text(main_source)
    removed = build(entry=main)
    assert removed[0]["native_partition_modules_skipped"] >= 1
    assert removed[1:] == build(False, entry=main)[1:]

    program = read_program(root / "cache", main)
    directory = root / "cache" / "native-v1"
    (directory / f"object-{program['object_keys'][0]}.entry").unlink()
    (root / ("program.exe" if os.name == "nt" else "program")).unlink()
    main.write_text('# Only one missing native object needs reconstruction.\n' + main_source)
    recovered = build(entry=main)
    assert recovered[0]["frontend_reused"] and recovered[0]["objects_built"] == 1
    assert recovered[0]["native_partition_modules_built"] == 1
    assert recovered[0]["native_partition_modules_skipped"] >= 1
    assert recovered[1:] == build(False, entry=main)[1:]

    manifest = next(json.loads(path.read_bytes().split(b"\n", 2)[2])
                    for path in directory.glob('program-*.entry')
                    if str(main) in json.loads(path.read_bytes().split(b"\n", 2)[2])["files"])
    payload = directory / f"payload-{manifest['payload_keys']['native_units']}.entry"
    original = payload.read_bytes()
    payload.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
    main.write_text('# No native dependency access is needed.\n' + main_source)
    assert build(entry=main)[0]["frontend_reused"]
    helper.write_text(helper_source.replace('"changed"', '"changed again"'))
    assert 'corrupt cache entry' in build(entry=main, success=False)[1]
    payload.write_bytes(original)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
