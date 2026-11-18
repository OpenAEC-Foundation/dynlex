"""Object output reuses semantic products and restores a linkable artifact."""

import json
import subprocess
from pathlib import Path


def check_incremental_object_output(root, compiler, flags):
    root.mkdir()
    main = root / "main.dl"
    definitions = root / "definitions.dl"
    main.write_text('import definitions.dl\n'
                    'integer means: @intrinsic("type", "int")\n'
                    'exposed function [stable] {integer:item}:\n'
                    '    execute:\n'
                    '        @intrinsic("return", @intrinsic("add", item, 10))\n')
    source = ('exposed function [calculate] {integer:item}:\n'
              '    execute:\n'
              '        @intrinsic("return", @intrinsic("add", item, 1))\n')
    definitions.write_text(source)
    output = root / "main.o"

    def build(incremental=True, *, entry=main, target=None, library=True, success=True):
        command = [str(compiler), str(entry), "--emit-object", *flags]
        if library:
            command.append("--no-main")
        if target is not None:
            command.extend(("-o", str(target)))
        command += (["--cache-dir", str(root / "cache"), "--incremental-stats"]
                    if incremental else ["--no-incremental"])
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=30)
        stderr = result.stderr.decode().replace("\r\n", "\n")
        assert result.returncode == (0 if success else 1), (command, stderr)
        lines = stderr.splitlines(keepends=True)
        stats = next((json.loads(line[13:]) for line in lines if line.startswith("incremental: ")), None)
        diagnostics = "".join(line for line in lines if not line.startswith("incremental: "))
        artifact = (result.stdout if target == "-" else (target or entry.with_suffix(".o")).read_bytes()) if success else None
        return stats, diagnostics, artifact

    fresh = build(False)
    # LLVM's object writer needs a seekable destination, including when
    # stdout is a pipe. ELF debug output also needs a file for objcopy.
    assert build(False, target="-")[1:] == fresh[1:]

    def parity(**arguments):
        incremental = build(**arguments)
        fresh_target = root / "clean.o"
        clean = build(False, target=fresh_target, **arguments)
        assert incremental[1:] == clean[1:]
        assert incremental[0]["link_ms"] == 0
        return incremental

    first = parity()
    assert first[0]["objects_built"] == 1
    original_timestamp = output.stat().st_mtime_ns
    unchanged = build()
    assert unchanged[0]["frontend_reused"] and unchanged[0]["objects_built"] == 0
    assert output.stat().st_mtime_ns == original_timestamp

    for number in (2, 3):
        definitions.write_text(source.replace("item, 1)", f"item, {number})"))
        edited = parity()
        assert edited[0]["functions_ir_reused"] > 0
        assert edited[0]["objects_built"] == 1 and edited[2] != first[2]

    for damage in (None, b"damaged object"):
        if damage is None:
            output.unlink()
        else:
            output.write_bytes(damage)
        restored = build()
        assert restored[0]["frontend_reused"] and restored[0]["objects_built"] == 0
        assert restored[0]["objects_reused"] == 1 and restored[0]["link_ms"] == 0
        assert restored[1:] == edited[1:]

    for target in (root / "alternate.o", "-", "-"):
        restored = build(target=target)
        assert restored[0]["objects_reused"] == 1 and restored[1:] == edited[1:]

    manifest = Path(edited[0]["cache_entry"])
    record = json.loads(manifest.read_bytes().split(b"\n", 2)[2])
    (manifest.parent / f'object-{record["object_keys"][0]}.entry').unlink()
    output.unlink()
    restored = build()
    assert restored[0]["frontend_reused"] and restored[0]["objects_built"] == 1
    assert restored[1:] == edited[1:]

    unwritable = root / "missing" / "output.o"
    assert build(target=unwritable, success=False)[1] == build(False, target=unwritable, success=False)[1]

    config = root / "config.dl"
    messages = ('dynlex options:\n    messages:\n'
                '        explicit parameter ambiguity: "First warning for {parameter}"\n')
    config.write_text(messages)
    warning = root / "warning.dl"
    warning.write_text('integer means: @intrinsic("type", "int")\n'
                       'value means: 1\n'
                       'to show {integer:value}: @intrinsic("discard", value)\nshow 2\n')
    before = parity(entry=warning, library=False)
    assert "First warning for value" in before[1]
    original_timestamp = warning.with_suffix(".o").stat().st_mtime_ns
    config.write_text(messages.replace("First", "Second"))
    after = parity(entry=warning, library=False)
    assert "Second warning for value" in after[1] and after[2] == before[2]
    assert after[0]["frontend_reused"] and after[0]["objects_built"] == 0
    assert after[0]["objects_reused"] == 0 and warning.with_suffix(".o").stat().st_mtime_ns == original_timestamp
