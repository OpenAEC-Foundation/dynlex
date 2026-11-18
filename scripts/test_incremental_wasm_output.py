"""Reuse target-specific products while preserving native-CLI WebAssembly output."""

import json
import subprocess
from pathlib import Path


def check_incremental_wasm_output(root, compiler, flags):
    root.mkdir()
    main = root / "main.dl"
    provider = root / "provider.dl"
    main.write_text('import provider.dl\n'
                    'integer means: @intrinsic("type", "int")\n'
                    'to get stable item:\n    @intrinsic("return", @intrinsic("add", item, 10))\n'
                    '@intrinsic("call", "env", "record_text", @intrinsic("type", "nothing"), "stable data")\n'
                    '@intrinsic("store", number, @intrinsic("call", "env", "read_number", integer))\n'
                    '@intrinsic("return", @intrinsic("add", base number, stable number))\n')
    source = 'to get base item:\n    @intrinsic("return", @intrinsic("add", item, 1))\n'
    provider.write_text(source)
    output = root / "main.dl.wasm"
    clean_folder = root / "clean"
    clean_folder.mkdir()

    def build(incremental=True, *, entry=main, target=None, success=True):
        command = [str(compiler), str(entry), "--emit-wasm", *flags]
        output_path = target or entry.with_suffix(".dl.wasm")
        if not incremental and output_path != "-" and success:
            # LLD includes the output basename in the Wasm name section.
            output_path = clean_folder / output_path.name
        if not incremental or target is not None:
            command.extend(("-o", str(output_path)))
        command += (["--cache-dir", str(root / "cache"), "--incremental-stats"]
                    if incremental else ["--no-incremental"])
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=30)
        stderr = result.stderr.decode().replace("\r\n", "\n")
        assert result.returncode == (0 if success else 1), (command, stderr)
        lines = stderr.splitlines(keepends=True)
        stats = next((json.loads(line[13:]) for line in lines if line.startswith("incremental: ")), None)
        diagnostics = "".join(line for line in lines if not line.startswith("incremental: "))
        artifact = None
        if success:
            artifact = result.stdout if output_path == "-" else output_path.read_bytes()
        return stats, diagnostics, artifact

    def parity(**arguments):
        cached = build(**arguments)
        assert cached[1:] == build(False, **arguments)[1:]
        return cached

    first = parity()
    assert first[2].startswith(b"\x00asm")
    count = first[0]["objects_built"]
    assert count > 1
    timestamp = output.stat().st_mtime_ns
    unchanged = build()
    assert unchanged[0]["frontend_reused"] and unchanged[0]["codegen_and_link_ms"] == 0
    assert output.stat().st_mtime_ns == timestamp

    for literal in (2, 3):
        provider.write_text(source.replace("item, 1)", f"item, {literal})"))
        edit = parity()
        assert edit[0]["source_preparation_files_reused"] >= 1
        assert edit[0]["functions_ir_reused"] > 0
        assert edit[0]["objects_built"] >= 1 and edit[0]["objects_reused"] >= 1
        assert edit[0]["objects_built"] + edit[0]["objects_reused"] == count
        assert edit[2] != first[2]

    output.write_bytes(b"damaged")
    for destination in (output, root / "renamed.wasm", "-", "-"):
        restored = parity(target=destination)
        assert restored[0]["frontend_reused"]
        assert restored[0]["objects_built"] == 0 and restored[0]["objects_reused"] == count
        assert restored[0]["linked_cached_objects"]
    assert (root / "renamed.wasm").read_bytes() != edit[2]

    manifest_path = Path(edit[0]["cache_entry"])
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    (manifest_path.parent / f'object-{manifest["object_keys"][0]}.entry').unlink()
    output.unlink()
    rebuilt = parity()
    assert rebuilt[0]["frontend_reused"] and rebuilt[0]["objects_built"] == 1
    assert rebuilt[0]["objects_reused"] == count - 1
    assert rebuilt[2] == edit[2]

    bad_output = root / "missing-directory" / "main.wasm"
    parity(target=bad_output, success=False)
    warning = root / "warning.dl"
    warning.write_text('integer means: @intrinsic("type", "int")\nvalue means: 1\n'
                       'to show {integer:value}: @intrinsic("discard", value)\nshow 2\n')
    config = root / "config.dl"
    message = ('dynlex options:\n    messages:\n'
               '        explicit parameter ambiguity: "First warning for {parameter}"\n')
    config.write_text(message)
    warned = parity(entry=warning)
    assert "First warning for value" in warned[1]
    path = warning.with_suffix(".dl.wasm")
    timestamp = path.stat().st_mtime_ns
    config.write_text(message.replace("First", "Second"))
    renamed = parity(entry=warning)
    assert "Second warning for value" in renamed[1] and renamed[2] == warned[2]
    assert renamed[0]["frontend_reused"] and renamed[0]["codegen_and_link_ms"] == 0
    assert path.stat().st_mtime_ns == timestamp


def check_wasm_optimized_function_reuse(root, compiler):
    root.mkdir()
    entry = root / "main.dl"
    output = root / "main.dl.wasm"
    source = '''integer32 means: @intrinsic("type", "int", 32)
function inc {integer32:item}:
    execute:
        @intrinsic("store", result, @intrinsic("call", "host", "number", integer32, item))
        @intrinsic("return", @intrinsic("add", result, item))
function unchanged {integer32:first} and {integer32:second}:
    execute:
        @intrinsic("return", @intrinsic("add", inc first, @intrinsic("add", inc second, inc first)))
@intrinsic("call", "host", "save_callback", @intrinsic("type", "nothing"), @intrinsic("function", "unchanged $ and $"))
@intrinsic("return", 41)
'''

    def build(flags):
        return subprocess.run([str(compiler), entry.name, "--emit-wasm", "-O2", *flags],
                              cwd=root, capture_output=True, text=True, check=True, timeout=30)

    cache = ["--cache-dir", str(root / "cache"), "--incremental-stats"]
    entry.write_text(source)
    build(cache)
    before = output.read_bytes()
    entry.write_text(source.replace("return\", 41", "return\", 42"))
    edited = build(cache)
    stats, = (json.loads(line[13:]) for line in edited.stderr.splitlines() if line.startswith("incremental: "))
    assert stats["functions_ir_reused"] > 0
    assert stats["objects_built"] == 1 and stats["objects_reused"] == 1
    assert stats["native_functions_lowered"] == 1
    after = output.read_bytes()
    assert before != after
    build(["--no-incremental"])
    assert output.read_bytes() == after
