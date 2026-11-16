"""Invalidate callers through non-native forwarding bodies when overloads change."""


def check_transitive_flex_overload(root, build):
    main = root / "transitive_flex_overload.dl"
    source = ('integer means: @intrinsic("type", "int")\n'
              'flex function choice {item}:\n'
              '    replacement: @intrinsic("add", item, 3)\n'
              'flex function forward {item}:\n'
              '    replacement: choice item\n'
              'to runner: @intrinsic("variadic call", "libc", "printf", '
              '@intrinsic("type", "nothing"), 1, "%d\\n", '
              'forward @intrinsic("command line argument count"))\nrunner\n')
    main.write_text(source)
    assert build(entry=main)[2] == b"3\n"
    specific = ('flex function choice {integer:item}:\n'
                '    replacement: @intrinsic("multiply", item, 2)\n')
    for text, expected in ((source + specific, b"0\n"),
                           (source + specific.replace('"multiply", item, 2', '"add", item, 4'), b"4\n"),
                           (source, b"3\n")):
        main.write_text(text)
        edited = build(entry=main)
        assert edited[1:] == build(False, entry=main)[1:]
        assert edited[2] == expected
