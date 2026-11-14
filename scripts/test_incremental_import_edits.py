"""Content digests and import precedence remain live across native cache reuse."""

import os


def check_source_and_import_edits(root, main, source, build):
    original_time = main.stat().st_mtime_ns
    main.write_text("this is an undefined expression\n")
    os.utime(main, ns=(original_time, original_time))
    broken = build(success=False)
    reference = build(False, success=False)
    assert broken[1] == reference[1], "invalid source reused old diagnostics or code"
    main.write_text(source)
    assert build()[1:] == build(False)[1:]

    nested = root / "nested"
    nested.mkdir()
    imported_main = nested / "main.dl"
    imported_main.write_text("import helper.dl\n")
    helper = root / "helper.dl"
    helper.write_text(source)
    first_import = build(entry=imported_main)
    assert first_import[1:] == build(False, entry=imported_main)[1:]
    assert build(entry=imported_main)[0]["frontend_reused"]
    (nested / "helper.dl").write_text(source.replace('"alpha"))', '"delta"))'))
    shadow = build(entry=imported_main)
    assert not shadow[0]["frontend_reused"], "new higher-priority import was ignored"
    assert shadow[1:] == build(False, entry=imported_main)[1:]
    (nested / "helper.dl").unlink()
    helper.write_text(source.replace('"alpha"))', '"omega"))'))
    assert build(entry=imported_main)[1:] == build(False, entry=imported_main)[1:]
    helper.unlink()
    assert build(success=False, entry=imported_main)[1] == build(False, success=False, entry=imported_main)[1]
