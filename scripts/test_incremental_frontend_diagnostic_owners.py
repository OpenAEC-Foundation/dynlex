"""A frontend warning must not disable unrelated body-resolution products."""

import json
from pathlib import Path

from test_incremental_matcher_work import matcher_record_keys
from test_incremental_program_cache import read_program


def retained_matcher_keys(manifest, directory):
    index = json.loads((directory / f'matcher-{manifest["matcher_key"]}.entry')
                       .read_bytes().split(b"\n", 2)[2])
    keys = set()
    for descriptor in index.values():
        keys.update(matcher_record_keys(directory / f'matcher-segment-{descriptor["payload"]}.entry'))
    return keys


def check_frontend_diagnostic_units(root, build):
    main = root / "frontend-diagnostic-units.dl"
    warning = 'to choose value: @intrinsic("discard", value)\n'
    healthy = 'to consume item: @intrinsic("discard", item)\n'
    source = ('value means: 9\n' + warning + healthy + 'consume 2\n'
              '@intrinsic("discard", 1)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert "Ambiguous single-word reference" in cold[1], cold[1]

    def parity(*, success=True):
        result = build(entry=main, success=success)
        assert result[1:] == build(False, entry=main, success=success)[1:]
        return result

    edited_source = source.replace('"discard", 1', '"discard", 2')
    main.write_text(edited_source)
    edited = parity()
    assert edited[0]["classification_units_reused"] >= 1, \
        "one body warning disabled classification reuse for its whole file"
    assert edited[0]["body_resolution_units_reused"] >= 1, \
        "one body warning disabled resolution reuse for its whole file"
    assert "Ambiguous single-word reference" in edited[1], edited[1]

    main.write_text(edited_source.replace(warning, 'to choose value: @intrinsic("discard", 0)\n'))
    removed = parity()
    assert "Ambiguous single-word reference" not in removed[1], removed[1]
    assert removed[0]["classification_units_reused"] >= 1, removed[0]

    main.write_text(edited_source)
    restored = parity()
    assert "Ambiguous single-word reference" in restored[1], restored[1]
    program = read_program(root / "cache", main)
    projection = program["all_function_projection"]
    healthy_path = next(path for path, unit in projection["inactive"].items()
                        if unit["header"] == "consume item:")
    healthy_key = projection["matcher_projection"]["body_units"][healthy_path].encode()
    manifest_path = Path(restored[0]["cache_entry"])

    # A failure in a different unit must not overwrite the successful products.
    main.write_text(edited_source.replace(healthy, 'to consume item: nonexistent call\n'))
    parity(success=False)
    failure_path = manifest_path.with_name(manifest_path.name.replace("program-", "errors-", 1))
    failure = json.loads(failure_path.read_bytes().split(b"\n", 2)[2])
    assert healthy_key in retained_matcher_keys(failure, manifest_path.parent), \
        "a partial matcher snapshot discarded the last successful body product"
    main.write_text(edited_source.replace('"discard", 2)\n', '"discard", 3)\n'))
    repaired = parity()
    assert repaired[0]["body_resolution_units_reused"] >= 1, repaired[0]

    # A complete build can prove a removed unit is dead and prune its product.
    main.write_text(edited_source.replace(healthy, "").replace("consume 2\n", ""))
    parity()
    pruned = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    assert healthy_key not in retained_matcher_keys(pruned, manifest_path.parent), \
        "a completed matcher snapshot retained a removed body product"

    # Diagnostic ownership cannot hide a newly visible provider's parameter change.
    main.write_text(edited_source + 'item means: 3\n')
    parity(success=False)
    check_header_diagnostic_units(root, build)


def check_header_diagnostic_units(root, build):
    main = root / "header-diagnostic-units.dl"
    source = ('to show value as the value: @intrinsic("discard", value)\n'
              'to consume item: @intrinsic("discard", item)\n'
              'show 1 as the value\nconsume 2\n@intrinsic("discard", 1)\n')
    main.write_text(source)
    cold = build(entry=main)
    assert "Repeated pattern word" in cold[1], cold[1]
    main.write_text(source.replace('"discard", 1', '"discard", 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    assert edited[0]["classification_units_reused"] >= 1, \
        "a declaration warning was assigned to every body in its file"
    assert edited[0]["body_resolution_units_reused"] >= 1, edited[0]
