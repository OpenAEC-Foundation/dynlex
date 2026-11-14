"""Completed source products survive failures before semantic stages start."""


def check_source_checkpoints(root, build):
    project = root / "source-checkpoints"
    project.mkdir()
    main, helper, missing = (project / name for name in
                             ("main.dl", "helper.dl", "missing.dl"))
    source = "import helper.dl\nimport missing.dl\n"
    main.write_text(source)
    helper.write_text('@intrinsic("discard", 0)\n')

    failed = build(entry=main, success=False)
    assert failed[1:] == build(False, entry=main, success=False)[1:]
    assert failed[0]["source_preparation_files_processed"] == 2, failed[0]
    repeated = build(entry=main, success=False)
    assert repeated[1:] == failed[1:]
    assert repeated[0]["source_preparation_files_reused"] == 2, repeated[0]
    assert repeated[0]["source_preparation_files_processed"] == 0, repeated[0]
    assert repeated[0]["function_body_inference_passes"] == 0, repeated[0]
    assert repeated[0]["objects_built"] == 0, repeated[0]

    # Message changes must not evaluate already completed source or code units.
    config = project / "config.dl"
    for label in ("checkpoint alpha", "checkpoint beta"):
        config.write_text('dynlex options:\n    messages:\n'
                          f'        failed to import source file: "{label}: {{path}}"\n')
        wording = build(entry=main, success=False)
        assert wording[1:] == build(False, entry=main, success=False)[1:]
        assert label in wording[1]
        assert wording[0]["source_preparation_files_reused"] == 2, wording[0]
        assert wording[0]["source_preparation_files_processed"] == 0, wording[0]
        assert wording[0]["function_body_inference_passes"] == 0, wording[0]
        assert wording[0]["codegen_and_link_ms"] == 0, wording[0]
    config.unlink()

    missing.write_text('@intrinsic("discard", 0)\n')
    repaired = build(entry=main)
    assert repaired[1:] == build(False, entry=main)[1:]
    assert repaired[0]["source_preparation_files_reused"] == 2, repaired[0]
    assert repaired[0]["source_preparation_files_processed"] == 1, repaired[0]

    # An earlier failure must retain previously completed, now-unvisited units.
    main.write_text("import missing-first.dl\n" + source)
    earlier = build(entry=main, success=False)
    assert earlier[1:] == build(False, entry=main, success=False)[1:]
    main.write_text(source)
    missing.write_text('@intrinsic("discard", 1)\n')
    recovered = build(entry=main)
    assert recovered[1:] == build(False, entry=main)[1:]
    assert recovered[0]["source_preparation_files_reused"] == 2, recovered[0]
    assert recovered[0]["source_preparation_files_processed"] == 1, recovered[0]

    # A preprocessing error is also before ImportedFiles, after healthy imports.
    main.write_text(source + '@intrinsic("discard", formatted "{unterminated")\n')
    malformed = build(entry=main, success=False)
    assert malformed[1:] == build(False, entry=main, success=False)[1:]
    assert "Invalid formatted string" in malformed[1]
    main.write_text(source)
    missing.write_text('@intrinsic("discard", 2)\n')
    recovered = build(entry=main)
    assert recovered[1:] == build(False, entry=main)[1:]
    assert recovered[0]["source_preparation_files_reused"] == 2, recovered[0]

    # Complete snapshots still drop products for removed imports.
    main.write_text("import helper.dl\n")
    build(entry=main)
    main.write_text(source)
    restored_import = build(entry=main)
    assert restored_import[1:] == build(False, entry=main)[1:]
    assert restored_import[0]["source_preparation_files_reused"] == 1, restored_import[0]
    assert restored_import[0]["source_preparation_files_processed"] == 2, restored_import[0]
