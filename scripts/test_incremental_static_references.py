"""Static objects retain identity when cached functions are imported in a new order."""

import json
from pathlib import Path


def check_static_reference_identities(root, build):
    main = root / "static_reference_identities.dl"
    source = (
        'function first: execute: @intrinsic("return", @intrinsic("static reference", 7))\n'
        'function second: execute: @intrinsic("return", @intrinsic("static reference", 9))\n'
        'function third: execute: @intrinsic("return", @intrinsic("static reference", 7))\n'
        '@intrinsic("store", a, first)\n'
        '@intrinsic("store", b, second)\n'
        '@intrinsic("store", c, third)\n'
        '@intrinsic("return", @intrinsic("select", @intrinsic("or", '
        '@intrinsic("not equal", a, first), @intrinsic("equal", a, c)), 1, 0))\n'
    )

    def static_symbols(stats):
        entry = Path(stats["cache_entry"])
        program = json.loads(entry.read_bytes().split(b"\n", 2)[2])
        payload = entry.parent / f"payload-{program['payload_keys']['native_units']}.entry"
        units = json.loads(payload.read_bytes().split(b"\n", 2)[2])
        return {name: value for name, value in units["symbols"].items()
                if "section.data" in name or ".dynlex.static." in name}

    main.write_text(source)
    original = build(entry=main)
    symbols = static_symbols(original[0])
    assert len(symbols) == 3, "distinct static expressions were merged"
    reordered = source.replace('@intrinsic("store", a, first)\n@intrinsic("store", b, second)\n',
                               '@intrinsic("store", b, second)\n@intrinsic("store", a, first)\n')
    main.write_text(reordered)
    edited = build(entry=main)
    assert edited[0]["functions_ir_reused"] >= 3
    edited_symbols = static_symbols(edited[0])
    # Drop this program's manifest to run a cold frontend in the same namespace.
    Path(edited[0]["cache_entry"]).unlink()
    fresh = build(entry=main)
    assert not fresh[0]["frontend_reused"]
    assert original[1:] == edited[1:] == fresh[1:] == build(False, entry=main)[1:]
    assert edited_symbols == static_symbols(fresh[0]), "static identities depend on emission order"
    assert edited_symbols == symbols, "reordering calls renamed unchanged static objects"
