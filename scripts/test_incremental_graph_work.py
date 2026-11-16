"""Dependency publication probes count real work and remain zero when skipped."""


def check_dependency_graph_work(root, build):
    from test_incremental_source_graph import check_source_only_graph_reuse
    check_source_only_graph_reuse(root, build)
    main = root / "dependency_graph_work.dl"
    source = ('to get leaf {item}:\n'
              '    @intrinsic("return", @intrinsic("add", item, 1))\n'
              'to get outer {item}:\n'
              '    @intrinsic("return", leaf item)\n'
              '@intrinsic("discard", outer @intrinsic("command line argument count"))\n')
    main.write_text(source)
    build(entry=main)
    main.write_text(source.replace('item, 1', 'item, 2'))
    edited = build(entry=main)
    assert edited[1:] == build(False, entry=main)[1:]
    stats = edited[0]
    phases = tuple(f"dependency_graph_{phase}_ms" for phase in
                   ("setup", "sections", "reverse", "units", "lookups"))
    nested = tuple(f"dependency_graph_{phase}_ms" for phase in
                   ("matched", "unit_capture", "unit_normalize", "unit_globals"))
    counts = ("dependency_graph_sections_visited", "dependency_graph_units_normalized",
              "dependency_graph_global_edges_visited")
    assert stats[counts[0]] > 0 and stats[counts[1]] > 0
    assert all(stats[name] >= 0 for name in (*phases, *nested, *counts))
    assert sum(stats[name] for name in phases) <= stats["dependency_graph_ms"]
    assert stats[nested[0]] <= stats[phases[1]]
    assert sum(stats[name] for name in nested[1:]) <= stats[phases[3]]
    for comment in (False, True):
        if comment:
            main.write_text("# unchanged meaning\n" + main.read_text())
        skipped = build(entry=main)
        assert skipped[1:] == edited[1:]
        assert all(skipped[0][name] == 0 for name in (*phases, *nested, *counts)), \
            "dependency publication probes reported work on a skipped frontend"
    main.write_text(source.replace('@intrinsic("add", item, 1)', '@intrinsic("construct", item)'))
    failure = build(entry=main, success=False)
    assert failure[1:] == build(False, entry=main, success=False)[1:]
    assert failure[0][counts[0]] > 0, "failed inference did not publish its dependency products"
    assert sum(failure[0][name] for name in phases) <= failure[0]["dependency_graph_ms"], \
        "failed dependency publication was omitted from the stage timer"
