"""Imported optional bindings keep their own declaration source locations."""

import re
import subprocess


def cross_file_sources(root):
    helper = root / "debug_origin_helper.dl"
    helper.write_text('flex section with saved value [at index|]:\n'
                      '    replacement:\n        @intrinsic("store", index, value)\n'
                      '        @intrinsic("execute body")\n')
    source = ('import debug_origin_helper.dl\n'
              'to get result {seed}:\n    with saved seed:\n'
              '        @intrinsic("return", seed)\n'
              '@intrinsic("variadic call", "libc", "printf", @intrinsic("type", "nothing"), '
              '1, "%d\\n", result @intrinsic("command line argument count"))\n'
              '@intrinsic("discard", 0)\n')
    return helper, source


def check_cross_file_local_debug_locations(compiler, root):
    _, source = cross_file_sources(root)
    main = root / "debug_origin.dl"
    main.write_text(source)
    output = root / "debug_origin.ll"
    subprocess.run([str(compiler), str(main), "-g", "-O0", "--no-incremental", "--emit-llvm",
                    "-o", str(output)], cwd=root, capture_output=True, check=True)
    text = output.read_text()
    records = dict(re.findall(r"^!(\d+) = (.*)$", text, re.MULTILINE))
    declarations = re.findall(r"#dbg_declare\([^\n]*?, !(\d+), !DIExpression\([^\n]*?\), !(\d+)\)", text)
    assert declarations, "fixture emitted no local declaration"
    for variable, location in declarations:
        expected = re.search(r"\bfile: !(\d+)", records[variable]).group(1)
        scope = re.search(r"\bscope: !(\d+)", records[location]).group(1)
        actual = re.search(r"\bfile: !(\d+)", records[scope]).group(1)
        assert actual == expected, "local debug location uses another file's line number"
