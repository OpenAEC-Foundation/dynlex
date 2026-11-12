"""Focused checks for the native program cache manifest and payload entries."""

import json
import os


def check_recent_cache_entries(root, build):
    active = ('integer means: @intrinsic("type", "int")\n'
              'exposed function preserved:\n    execute:\n'
              '        @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "preserved"))\n'
              '@intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "before"))\n')
    inactive = 'to unused: @intrinsic("discard", 1)\n@intrinsic("discard", 0)\n'
    for case, source, changed in (
        ("active", active, active.replace('"before"', '"after"')),
        ("inactive", inactive, inactive.replace('"discard", 1', '"discard", 2')),
        ("comment", inactive, "# comment\n" + inactive),
    ):
        main = root / ("recent_cache_" + case + ".dl")
        main.write_text(source)
        build(entry=main, flags=("-O2",))
        directory = root / "cache" / "native-v1"
        for entry in directory.glob("*.entry"):
            os.utime(entry, (1, 1))
        filler = directory / ("object-" + "f" * 64 + ".entry")
        assert not filler.exists()
        with filler.open("wb") as sparse:
            sparse.truncate(512 * 1024 * 1024)
        os.utime(filler, (2, 2))
        main.write_text(changed)
        edited = build(entry=main, flags=("-O2",))
        if case == "active":
            assert edited[0]["optimization_units_reused"] > 0, edited[0]
        elif case == "inactive":
            assert edited[0]["type_inference_reused"] and edited[0]["cache_payloads_loaded"] == 2, edited[0]
        else:
            assert edited[0]["frontend_reused"] and edited[0]["cache_payloads_loaded"] == 0, edited[0]
        repeated = build(entry=main, flags=("-O2",))
        assert repeated[0]["frontend_reused"], f"eviction removed retained dependencies after {case} edit"
        assert repeated[1:] == edited[1:] and not filler.exists()


def check_cache_eviction(root, build):
    namespace = root / "cache" / "native-v1"
    unrelated = namespace / "notes.entry"
    unrelated.write_text("user file")
    old_entries = []
    for prefix, digit in (("object-", "f"), ("parser-", "e"),
                          ("optimization-input-", "d"), ("optimization-", "c"),
                          ("inference-summary-", "b")):
        old = namespace / (prefix + digit * 64 + ".entry")
        with old.open("wb") as sparse:
            sparse.truncate(512 * 1024 * 1024 + 1)
        os.utime(old, (1, 1))
        old_entries.append(old)
    assert build()[0]["frontend_reused"]
    assert all(not path.exists() for path in old_entries), "eviction retained old cache entries"
    assert unrelated.read_text() == "user file", "eviction deleted the wrong entry"
    return namespace


def check_integration_builds(project, build):
    for suite, fixture in (
        ("integration", "json_serialization"),
        ("integration", "string_lifecycle_counts"),
        ("integration", "managed_array_pointer_lifecycle"),
        ("required", "recursive_store_reinference"),
        ("required", "overloaded_type_constraint_inference"),
    ):
        entry = project / "tests" / suite / fixture / "main.dl"
        reference = build(False, entry=entry)
        generated = build(entry=entry)
        assert generated[1:] == reference[1:], f"partitioned code changed behavior: {fixture}"
        reused = build(entry=entry)
        assert reused[0]["frontend_reused"] and reused[0]["link_ms"] == 0
        assert reused[0]["objects_built"] == 0
        assert reused[1:] == reference[1:], f"cached code changed behavior: {fixture}"


def decode_graph(encoded):
    paths = encoded["paths"]
    unit_keys, unit_sources = encoded["inference_unit_keys"], encoded["inference_unit_sources"]
    assert len(paths) == len(encoded["forward"]) == len(encoded["instantiations"])
    return {
        "forward": {path: [paths[index] for index in edges]
                    for path, edges in zip(paths, encoded["forward"])},
        "reverse": {paths[index]: [paths[caller] for caller in callers]
                    for index, callers in encoded["reverse"]},
        "instantiations": {path: {key: [paths[index] for index in edges]
                                  for key, edges in variants.items()}
                           for path, variants in zip(paths, encoded["instantiations"])},
        "callable_lookups": {
            key: {**lookup, "owners": [paths[index] for index in lookup["owners"]]}
            for key, lookup in encoded["callable_lookups"].items()
        },
        "inference_reads": {paths[owner]: [paths[index] for index in reads]
                            for owner, reads in encoded["inference_reads"]},
        "inference_units": {
            path: {unit_keys[unit[0]]: {
                "sources": {paths[unit_sources[index][0]]: unit_sources[index][1] for index in unit[1]},
                "callees": [[paths[index], unit_keys[variant]] for index, variant in unit[2]],
                "lookups": unit[3],
                "values": [[paths[index], unit_keys[variant], arguments, result]
                           for index, variant, arguments, result in unit[4]],
            } for unit in variants}
            for path, variants in zip(paths, encoded["inference_units"])
        },
    }



def decode_inference_summary(encoded):
    types = []
    for node in encoded["types"]:
        value = list(node)
        if node[0] in (7, 8, 9):
            value[5] = types[node[5]]
        elif node[0] == 10 and len(node) == 6:
            value[5] = [types[index] for index in node[5]]
        elif node[0] == 11:
            value[3] = types[node[3]]
            value[4] = [types[index] for index in node[4]]
        elif node[0] == 12 and node[4] is not None:
            value[4] = types[node[4]]
        types.append(value)

    def constant(value):
        result = list(value)
        if value[0] in ("type", "constraint"):
            position = 2 if value[0] == "type" else 1
            identity = value[position]
            result[position] = [identity[0], [types[index] for index in identity[1]]]
            if value[0] == "type":
                result[1] = types[value[1]]
        elif value[0] == "function":
            result[4] = [types[index] for index in value[4]]
            result[5] = types[value[5]]
        return result

    def constants(values):
        return [[name, constant(value)] for name, value in values]

    def scope(value):
        if value is None:
            return None
        if "parent" in value:
            return {**value, "parent": scope(value["parent"])}
        return {**value, "argument_types": [types[index] for index in value["argument_types"]],
                "compile_time_parameters": constants(value["compile_time_parameters"])}

    def provenance(value):
        return {**value, "targets": [[definition, scope(invocation)]
                                     for definition, invocation in value["targets"]]}

    summary = encoded["summary"]
    effects = summary["global_effects"]
    parameters = summary["parameters"]
    return {
        **summary,
        "argument_types": [types[index] for index in summary["argument_types"]],
        "compile_time_parameters": constants(summary["compile_time_parameters"]),
        "parameters": {
            **parameters,
            "types": {name: types[index] for name, index in parameters["types"].items()},
            "outputs": {name: types[index] for name, index in parameters["outputs"].items()},
            "constants": {name: constant(value) for name, value in parameters["constants"].items()},
        },
        "return_type": types[summary["return_type"]],
        "pure_values": [[[constant(argument) for argument in arguments], constant(value)]
                        for arguments, value in summary["pure_values"]],
        "return_provenance": provenance(summary["return_provenance"]),
        "global_effects": {
            **effects,
            "writes": {name: provenance(value) for name, value in effects["writes"].items()},
            "constants": {name: constant(value) for name, value in effects["constants"].items()},
            "escaped": provenance(effects["escaped"]),
        },
    }


def read_program(cache, entry):
    matches = []
    directory = cache / "native-v1"
    for path in directory.glob("program-*.entry"):
        program = json.loads(path.read_bytes().split(b"\n", 2)[2])
        if str(entry) not in program["files"]:
            continue
        for field, key in program["payload_keys"].items():
            payload = (directory / f"payload-{key}.entry").read_bytes().split(b"\n", 2)[2]
            if field == "generated_ir":
                program[field] = payload
            elif field == "section_dependency_graph":
                program[field] = decode_graph(json.loads(payload))
            elif field == "inference_summaries":
                program[field] = {
                    section: {
                        variant: decode_inference_summary(json.loads(
                            (directory / f"inference-summary-{reference['payload']}.entry")
                            .read_bytes().split(b"\n", 2)[2]))
                        for variant, reference in variants.items()
                    }
                    for section, variants in json.loads(payload).items()
                }
            else:
                program[field] = json.loads(payload)
        matches.append(program)
    assert len(matches) == 1, "expected one cached program"
    return matches[0]


def check_program_payloads(root, build):
    main = root / "program_payloads.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'to report {integer:left} {integer:right}: @intrinsic("discard", @intrinsic("call", "libc", "puts", integer, "first"))\n'
              'report 1 2\n')
    main.write_text(source)
    build(entry=main)
    directory = root / "cache" / "native-v1"
    manifest_path = next(path for path in directory.glob("program-*.entry")
                         if str(main) in json.loads(path.read_bytes().split(b"\n", 2)[2])["files"])
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    assert set(manifest["payload_keys"]) == {
        "all_function_projection", "section_dependency_graph", "inference_summaries", "generated_ir", "native_units",
        "optimization_units", "validation_units"
    }
    assert all(field not in manifest for field in manifest["payload_keys"])
    summary_blob = directory / f"payload-{manifest['payload_keys']['inference_summaries']}.entry"
    references = json.loads(summary_blob.read_bytes().split(b"\n", 2)[2])
    encoded = [json.loads((directory / f"inference-summary-{reference['payload']}.entry")
                         .read_bytes().split(b"\n", 2)[2])
               for variants in references.values() for reference in variants.values()]
    assert all(set(unit) == {"types", "summary"} for unit in encoded)
    arguments = [unit["summary"]["argument_types"] for unit in encoded]
    assert any(len(types) == 2 and types[0] == types[1] for types in arguments), "equal argument types were duplicated"
    payloads = [directory / f"payload-{key}.entry" for key in manifest["payload_keys"].values()]
    assert manifest_path.stat().st_size < sum(path.stat().st_size for path in payloads)
    before = {path: (path.stat().st_ino, path.read_bytes()) for path in payloads}
    main.write_text("# comment\n" + source)
    commented = build(entry=main)
    assert commented[0]["frontend_reused"] and commented[0]["objects_built"] == 0
    assert before == {path: (path.stat().st_ino, path.read_bytes()) for path in payloads}, \
        "comment edit rewrote unchanged program payloads"
    assert commented[1:] == build(False, entry=main)[1:]
    main.write_text("# comment\n" + source.replace('"first"', '"second"'))
    edited = build(entry=main)
    assert edited[0]["cache_payloads_loaded"] == 6
    assert edited[1:] == build(False, entry=main)[1:]
    edited_program = read_program(root / "cache", main)
    units = edited_program["section_dependency_graph"]["inference_units"]
    required = {(path, key) for path, variants in units.items() for key in variants if key != "*"}
    required.add(("root", "*"))
    required.update((path, edited_program["function_instantiations"][name])
                    for name, path in edited_program["function_sections"].items())
    pending = list(required)
    while pending:
        path, key = pending.pop()
        for callee in units[path][key]["callees"]:
            target = tuple(callee)
            if target not in required:
                required.add(target)
                pending.append(target)
    assert all((path, key) in required for path, variants in units.items() for key in variants), \
        "dependency capture built aggregate units outside the retained output closure"
    assert edited_program["generated_ir"].startswith(b"BC\xc0\xde")
    for field in ("matches", "match_entries"):
        assert all(isinstance(key, str) and len(key) == 64
                   for key in edited_program["all_function_projection"][field].values()), \
            "function match records were embedded instead of carrying reusable unit keys"
    updated = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    graph_blob = directory / f"payload-{updated['payload_keys']['section_dependency_graph']}.entry"
    assert isinstance(json.loads(graph_blob.read_bytes().split(b"\n", 2)[2])["forward"], list)
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
        "unused edit loaded dependency, inference, or generated-code payloads"
    assert unused[1:] == build(False, entry=main)[1:]
