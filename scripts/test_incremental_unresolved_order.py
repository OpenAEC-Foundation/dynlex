"""Reactivated bodies report unresolved references in source order."""

import re


def check_unresolved_diagnostic_order(root, build):
    main = root / "unresolved_diagnostic_order.dl"
    source = ('to report {text}: @intrinsic("discard", text)\n'
              'to spare: report "spare"\n'
              'to idle: report "idle"\n'
              'spare\n')
    main.write_text(source)
    build(entry=main)
    changed = source.replace('to report', 'to renamed').replace('report "idle"', 'report "changed"')
    main.write_text(changed)
    cached = build(entry=main, success=False)
    fresh = build(False, entry=main, success=False)
    assert cached[1:] == fresh[1:], \
        "cached body reactivation changed unresolved diagnostic order"
    lines = [int(value) for value in re.findall(r"\.dl:(\d+):", cached[1])]
    assert len(lines) == 3 and lines == sorted(lines)
    main.write_text(source)
    assert build(entry=main)[1:] == build(False, entry=main)[1:]
