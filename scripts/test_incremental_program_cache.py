"""Focused checks for the native program cache manifest and payload entries."""

import json


def read_program(cache, entry):
    matches = []
    directory = cache / "native-v1"
    for path in directory.glob("program-*.entry"):
        program = json.loads(path.read_bytes().split(b"\n", 2)[2])
        if str(entry) not in program["files"]:
            continue
        for field, key in program["payload_keys"].items():
            payload = (directory / f"payload-{key}.entry").read_bytes().split(b"\n", 2)[2]
            program[field] = payload.decode() if field == "ir" else json.loads(payload)
        matches.append(program)
    assert len(matches) == 1, "expected one cached program"
    return matches[0]


def check_program_payloads(root, build):
    main = root / "program_payloads.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to report: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
              'report\n')
    main.write_text(source)
    build(entry=main)
    directory = root / "cache" / "native-v1"
    manifest_path = next(path for path in directory.glob("program-*.entry")
                         if str(main) in json.loads(path.read_bytes().split(b"\n", 2)[2])["files"])
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    assert set(manifest["payload_keys"]) == {
        "all_function_projection", "section_dependency_graph", "inference_summaries", "ir"
    }
    assert all(field not in manifest for field in manifest["payload_keys"])
    payloads = [directory / f"payload-{key}.entry" for key in manifest["payload_keys"].values()]
    assert manifest_path.stat().st_size < sum(path.stat().st_size for path in payloads)
    before = {path: path.stat().st_mtime_ns for path in payloads}
    main.write_text("# comment\n" + source)
    commented = build(entry=main)
    assert commented[0]["frontend_reused"] and commented[0]["objects_built"] == 0
    assert before == {path: path.stat().st_mtime_ns for path in payloads}, \
        "comment edit rewrote unchanged program payloads"
    assert commented[1:] == build(False, entry=main)[1:]
    main.write_text("# comment\n" + source.replace('"first"', '"second"'))
    edited = build(entry=main)
    assert edited[0]["cache_payloads_loaded"] == 4
    assert edited[1:] == build(False, entry=main)[1:]
    assert read_program(root / "cache", main)["ir"]
    updated = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    graph_blob = directory / f"payload-{updated['payload_keys']['section_dependency_graph']}.entry"
    graph_blob.unlink()
    main.write_text("# comment\n" + source.replace('"first"', '"third"'))
    evicted = build(entry=main)
    assert not evicted[0]["frontend_reused"] and evicted[1:] == build(False, entry=main)[1:], \
        "missing dependency payload changed compilation"
    unused_source = main.read_text() + 'to unused: @intrinsic("discard", 1)\n'
    main.write_text(unused_source)
    build(entry=main)
    main.write_text(unused_source.replace('"discard", 1', '"discard", 2'))
    unused = build(entry=main)
    assert unused[0]["type_inference_reused"] and unused[0]["cache_payloads_loaded"] == 2, \
        "unused edit loaded inference summaries or generated code"
    assert unused[1:] == build(False, entry=main)[1:]
