"""Expose actual section work and time spent before a section-stage error."""

from test_incremental_declaration_projections import check_declaration_projection_units


def check_section_analysis_work(root, build):
    main = root / "section_analysis_work.dl"
    source = ('to saved {item}:\n'
              '    @intrinsic("store", {copy}, item)\n'
              '    @intrinsic("discard", copy)\n'
              'to runner: @intrinsic("discard", 0)\nrunner\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('"discard", 0', '"discard", 1'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    stats = edited[0]
    assert stats["section_header_units_evaluated"] > 0
    assert stats["section_body_lines_dispatched"] > 0
    assert stats["section_finalizations"] > 0
    assert stats["source_outline_nodes_restored"] > 0
    assert stats["source_outline_pattern_references_created"] > 0
    assert stats["source_outline_variables_created"] > 0
    phases = ("section_header_ms", "section_body_dispatch_ms", "section_finalize_ms", "section_locality_ms")
    assert all(stats[name] >= 0 for name in phases)
    assert sum(stats[name] for name in phases) <= stats["parse_ms"]
    assert stats["source_outline_restore_ms"] <= stats["parse_ms"]
    check_section_failure_work(root, build)
    check_declaration_projection_units(root, build)


def check_section_failure_work(root, build):
    main = root / "section_analysis_failure.dl"
    main.write_text('function:\n    unknown:\n')
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
    assert failure[0]["import_ms"] > 0
    assert failure[0]["parse_ms"] > 0, "failed section analysis hid the time already spent in that stage"
