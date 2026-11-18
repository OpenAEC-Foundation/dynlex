"""Failed constraint snapshots retain complete matched-source dependencies."""

import json
import shutil
from pathlib import Path

from cache_payloads import read_payload


def constraint_graph(result):
    entry = Path(result[0]["cache_entry"])
    manifest = json.loads(entry.with_name(entry.name.replace("program-", "errors-", 1))
                          .read_bytes().split(b"\n", 2)[2])
    key = manifest["constraint_state"]["payload_keys"]["graph"]
    return read_payload(entry.parent / f"payload-{key}.entry")


def check_constraint_graph(root, build):
    main = root / "deferred_constraint_graph.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'broken type means: 0\n'
              'to broken {broken type:value}: @intrinsic("discard", value)\n'
              'to first: @intrinsic("discard", integer)\n'
              'to second: @intrinsic("discard", integer)\n'
              '@intrinsic("discard", 0)\n')
    main.write_text(source)
    build(entry=main, success=False)
    changed = source.replace('to first: @intrinsic("discard", integer)',
                             'to first: @intrinsic("discard", 1)')
    main.write_text(changed)
    edited = build(entry=main, success=False)
    assert edited[1:] == build(False, entry=main, success=False)[1:]
    warm = constraint_graph(edited)
    saved = root / "constraint-graph-saved-cache"
    (root / "cache").rename(saved)
    try:
        fresh = build(entry=main, success=False)
        assert fresh[1:] == edited[1:]
        assert warm == constraint_graph(fresh), "failed constraint snapshot lost deferred match edges"
    finally:
        shutil.rmtree(root / "cache")
        saved.rename(root / "cache")
    assert edited[0]["body_resolution_units_deferred"] > 0
    assert edited[0]["matched_dependency_facts_reused"] > 0
    main.write_text(changed.replace('broken type means: 0', 'broken type means: integer'))
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]["matched_dependency_owners_demanded"] == 0, \
        "native dependency capture discarded the valid failed-stage match product"
