"""A leaf edit loads and persists only its concrete inference summary."""

import json

from cache_payloads import read_payload

from test_incremental_program_cache import read_program


def summary_manifest(root, entry):
    directory = root / "cache" / "native-v1"
    manifest = next(json.loads(path.read_bytes().split(b"\n", 2)[2])
                    for path in directory.glob("program-*.entry")
                    if str(entry) in json.loads(path.read_bytes().split(b"\n", 2)[2])["files"])
    key = manifest["payload_keys"]["inference_summaries"]
    return read_payload(directory / f"payload-{key}.entry")


def check_summary_unit_reuse(root, build):
    main = root / "summary_unit_reuse.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'function [leaf] {value}:\n    execute:\n'
        '        @intrinsic("return", @intrinsic("add", value, 1))\n'
    )
    for index in range(4):
        source += (f'function [unrelated{index}] {{value{index}}}:\n    execute:\n'
                   f'        @intrinsic("return", @intrinsic("add", value{index}, {10 + index}))\n')
    arguments = ', '.join(f'{name} @intrinsic("command line argument count")'
                          for name in ['leaf', *[f'unrelated{index}' for index in range(4)]])
    source += ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
               f'"%d %d %d %d %d\\n", {arguments}))\n')
    main.write_text(source)
    cold = build(entry=main)
    assert cold[2] == b"1 10 11 12 13\n"
    assert cold[0].get("inference_summary_units_encoded") == 5, "summaries are not persisted per variant"
    before = summary_manifest(root, main)
    records = [root / "cache" / "native-v1" / f"inference-summary-{reference['payload']}.entry"
               for variants in before.values() for reference in variants.values()]
    identities = {path: (path.stat().st_ino, path.read_bytes()) for path in records}
    program = read_program(root / "cache", main)
    leaf = next(path for path, declaration in program["all_function_projection"]["function_declarations"].items()
                if declaration["identity"]["header"].startswith("function [leaf]"))
    main.write_text(source.replace('"add", value, 1)', '"add", value, 2)'))
    edited = build(entry=main)
    assert edited[2] == b"2 10 11 12 13\n" and edited[0]["root_execution_reused"]
    assert edited[0]["inference_summary_units_loaded"] == 1, "a leaf edit expanded unrelated inference summaries"
    assert edited[0]["inference_summary_units_encoded"] == 1, "a leaf edit re-encoded unrelated inference summaries"
    assert edited[0]["inference_summary_units_written"] == 0
    assert identities == {path: (path.stat().st_ino, path.read_bytes()) for path in records}, \
        "an unchanged inference interface rewrote its immutable summary record"
    assert edited[0]["native_functions_lowered"] == 1
    assert edited[1:] == build(False, entry=main)[1:]
    after = summary_manifest(root, main)
    assert before.keys() == after.keys()
    assert all(before[path] == after[path] for path in before if path != leaf)
    assert before[leaf].keys() == after[leaf].keys()
    for key, prior in before[leaf].items():
        current = after[leaf][key]
        assert {name: value for name, value in prior.items() if name != "body"} == \
               {name: value for name, value in current.items() if name != "body"}, \
               "an unchanged interface acquired a different summary payload"
        assert prior["body"]["payload"] != current["body"]["payload"], "a body edit retained its old executable graph"
    assert edited[0]["inference_bodies_encoded"] == edited[0]["inference_bodies_written"] == 1
    main.write_text('# comment\n' + main.read_text())
    comment = build(entry=main)
    assert comment[0]["frontend_reused"]
    assert comment[0]["inference_summary_units_loaded"] == 0
    assert comment[0]["inference_summary_units_encoded"] == 0

    unrelated = next(path for path in after if path != leaf)
    reference = next(iter(after[unrelated].values()))
    (root / "cache" / "native-v1" / f"inference-summary-{reference['payload']}.entry").unlink()
    main.write_text(main.read_text().replace('"add", value, 2)', '"add", value, 3)'))
    evicted = build(entry=main)
    assert evicted[2] == b"3 10 11 12 13\n" and evicted[0]["root_execution_reused"]
    assert evicted[0]["inference_summary_units_loaded"] == 1
    assert unrelated not in summary_manifest(root, main), "an evicted unvisited summary remained in the manifest"
    main.write_text(main.read_text() + '@intrinsic("discard", 99)\n')
    restored = build(entry=main)
    assert restored[2] == b"3 10 11 12 13\n"
    assert restored[0]["inference_summary_units_loaded"] == 4
    assert restored[0]["inference_summary_units_encoded"] == 1
    assert restored[1:] == build(False, entry=main)[1:]
    check_evicted_pure_observations(root, build)
    check_evicted_function_observation(root, build)


def check_evicted_pure_observations(root, build):
    main = root / "evicted_pure_observations.dl"
    source = (
        'integer means: @intrinsic("type", "int")\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
        'function [compute] {value}:\n    execute:\n'
        '        if @intrinsic("equal", value, 4):\n            @intrinsic("return", 1)\n'
        '        @intrinsic("return", 2)\n'
        'to retained caller:\n'
        '    @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", compute 4))\n'
        '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
        'compute @intrinsic("command line argument count")))\n'
        'retained caller\n'
    )
    main.write_text(source)
    assert build(entry=main)[2] == b"2\n1\n"
    program = read_program(root / "cache", main)
    path = next(path for path, declaration in program["all_function_projection"]["function_declarations"].items()
                if declaration["identity"]["header"].startswith("function [compute]"))
    reference = next(iter(summary_manifest(root, main)[path].values()))
    (root / "cache" / "native-v1" / f"inference-summary-{reference['payload']}.entry").unlink()
    added = source + '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", compute 5))\n'
    main.write_text(added)
    assert build(entry=main)[2] == b"2\n1\n2\n"
    main.write_text(added.replace('"return", 1)', '"return", 3)'))
    edited = build(entry=main)
    assert edited[2] == b"2\n3\n2\n", "eviction lost the observation used by retained caller IR"
    assert edited[1:] == build(False, entry=main)[1:]


def check_evicted_function_observation(root, build):
    def output(value):
        return ('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, "%d\\n", '
                '@intrinsic("cast", @intrinsic("equal", ' + value + ', '
                '@intrinsic("cast", @intrinsic("function", "callback A"), pointer)), integer)))\n')

    selected = '@intrinsic("return", @intrinsic("cast", @intrinsic("function", "callback A"), pointer))'
    definition = (
        'integer means: @intrinsic("type", "int")\n'
        'pointer means: @intrinsic("add pointer depth", @intrinsic("type", "int", 8))\n'
        'section if condition:\n    replacement: @intrinsic("if", condition)\n'
        'function [callback A]:\n    execute:\n        @intrinsic("return", 1)\n'
        'function [callback B]:\n    execute:\n        @intrinsic("return", 2)\n'
        'function [choose] {value}:\n    execute:\n'
        '        if @intrinsic("equal", value, 4):\n            ' + selected + '\n'
        '        @intrinsic("return", @intrinsic("construct", pointer))\n'
        'to retained caller:\n    ' + output('choose 4') +
        'to touch: @intrinsic("discard", 1)\n'
    )
    for runtime, evict in ((False, False), (False, True), (True, True)):
        main = root / f"function_observation_{runtime}_{evict}.dl"
        prefix = output('choose @intrinsic("command line argument count")') if runtime else ''
        expected = (b"0\n" if runtime else b"")
        source = definition + prefix + 'retained caller\ntouch\n'
        main.write_text(source)
        assert build(entry=main)[2] == expected + b"1\n"
        program = read_program(root / "cache", main)
        path = next(path for path, declaration in program["all_function_projection"]["function_declarations"].items()
                    if declaration["identity"]["header"].startswith("function [choose]"))
        changed = source.replace('"discard", 1)', '"discard", 2)')
        main.write_text(changed)
        assert build(entry=main)[2] == expected + b"1\n"
        references = summary_manifest(root, main)
        assert path in references, "retained IR lost the coverage of an unrecorded constexpr result"
        reference = next(iter(references[path].values()))
        assert reference["observed_values"] or not reference["pure_values_complete"], \
            "a constexpr function result lost its invalidation metadata"
        if evict:
            (root / "cache" / "native-v1" / f"inference-summary-{reference['payload']}.entry").unlink()
        added = changed + output('choose 5')
        main.write_text(added)
        assert build(entry=main)[2] == expected + b"1\n0\n"
        main.write_text(added.replace(selected, selected.replace('callback A', 'callback B')))
        edited = build(entry=main)
        assert edited[2] == expected + b"0\n0\n", "a retained caller kept a stale constexpr function result"
        assert edited[1:] == build(False, entry=main)[1:]
