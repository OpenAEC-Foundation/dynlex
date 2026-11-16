"""LLVM output shares semantic units and preserves its independent artifact."""

import json
import os
import re
import subprocess
from pathlib import Path


def check_incremental_llvm_output(root, compiler, optimization="-O0"):
    root.mkdir()
    cache = root / "cache"
    main = root / "main.dl"
    helper = root / "helper.dl"
    output = root / "program.ll"
    main.write_text('import helper.dl\n'
                    'integer means: @intrinsic("type", "int")\n'
                    'to get stable item:\n    @intrinsic("return", @intrinsic("add", item, 10))\n'
                    '@intrinsic("store", number, @intrinsic("call", "libc", "getchar", integer))\n'
                    '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", integer, 1, '
                    '"%d %d\\n", base number, stable number))\n')
    source = 'to get base item:\n    @intrinsic("return", @intrinsic("add", item, 1))\n'
    helper.write_text(source)

    def build(incremental=True, success=True, entry=main, target=output, flags=()):
        destination = target if incremental else target.with_name(target.name + ".clean")
        command = [str(compiler), str(entry), optimization, "--emit-llvm", *flags, "-o", str(destination)]
        command += (["--cache-dir", str(cache), "--incremental-stats"]
                    if incremental else ["--no-incremental"])
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        assert result.returncode == (0 if success else 1), (command, result.stdout, result.stderr)
        lines = result.stderr.splitlines(keepends=True)
        statistics = [line for line in lines if line.startswith("incremental: ")]
        stats = json.loads(statistics[0][13:]) if statistics else None
        diagnostics = "".join(line for line in lines if not line.startswith("incremental: "))
        text = destination.read_bytes() if success else None
        # The module identifier can contain an output/inspector path; retain
        # every other byte of the actual LLVM product.
        if text and text.startswith(b"; ModuleID = "):
            text = text.split(b"\n", 1)[1]
        return stats, diagnostics, text

    def parity(**arguments):
        edited = build(**arguments)
        assert edited[1:] == build(False, **arguments)[1:]
        return edited

    first = parity()
    assert first[0]["llvm_outputs_built"] == 1
    assert first[0]["objects_built"] == 0 and first[0]["link_ms"] == 0
    original_time = output.stat().st_mtime_ns
    unchanged = build()
    assert unchanged[0]["frontend_reused"] and unchanged[0]["codegen_and_link_ms"] == 0
    assert output.stat().st_mtime_ns == original_time
    helper.write_text("# Imported-file comment\n" + source)
    commented = parity()
    assert commented[0]["frontend_reused"]
    assert output.stat().st_mtime_ns == original_time

    for number in (2, 3):
        helper.write_text(source.replace("item, 1)", f"item, {number})"))
        edited = parity()
        assert edited[0]["source_preparation_files_reused"] >= 1
        assert edited[0]["functions_ir_reused"] >= 1
        assert edited[0]["llvm_outputs_built"] == 1
        assert edited[0]["objects_built"] == 0 and edited[0]["link_ms"] == 0

    helper.write_text('to get base item:\n    missing provider item\n')
    parity(success=False)
    helper.write_text(source.replace("item, 1)", "item, 4)"))
    repaired = parity()
    expected = output.read_bytes()
    for damage in (None, b"invalid LLVM output\n"):
        if damage is None:
            output.unlink()
        else:
            output.write_bytes(damage)
        restored = build()
        assert restored[0]["frontend_reused"]
        assert restored[0]["llvm_outputs_restored"] == 1
        assert restored[0]["codegen_and_link_ms"] == 0
        assert output.read_bytes() == expected
        assert restored[1:] == repaired[1:]
    alternate = root / "alternate.ll"
    moved = build(target=alternate)
    assert moved[0]["frontend_reused"] and moved[0]["llvm_outputs_restored"] == 1
    assert alternate.read_bytes() == expected

    manifest_path = Path(repaired[0]["cache_entry"])
    manifest = json.loads(manifest_path.read_bytes().split(b"\n", 2)[2])
    artifact = manifest_path.parent / f'llvm-output-{manifest["llvm_output_key"]}.entry'
    artifact.unlink()
    output.unlink()
    rebuilt = build()
    assert rebuilt[0]["frontend_reused"] and rebuilt[0]["llvm_outputs_built"] == 1
    assert output.read_bytes() == expected

    for _ in range(2):
        streamed = subprocess.run([str(compiler), str(main), optimization, "--emit-llvm", "-o", "-",
                                   "--cache-dir", str(cache), "--incremental-stats"],
                                  cwd=root, capture_output=True, timeout=120)
        assert streamed.returncode == 0, streamed.stderr
        assert streamed.stdout == expected
    assert build()[0]["frontend_reused"]

    executable = root / ("program.exe" if os.name == "nt" else "program")
    native = subprocess.run([str(compiler), str(main), optimization, "--cache-dir", str(cache),
                             "--incremental-stats", "-o", str(executable)],
                            cwd=root, capture_output=True, text=True, timeout=120)
    assert native.returncode == 0, native.stderr
    native_stats = next(json.loads(line[13:]) for line in native.stderr.splitlines()
                        if line.startswith("incremental: "))
    assert native_stats["cache_entry"] != repaired[0]["cache_entry"]
    runtime = subprocess.run([str(executable)], cwd=root, input=b"A", capture_output=True, timeout=10)
    assert runtime.returncode == 0 and runtime.stdout == b"69 75\n", runtime
    assert build()[0]["frontend_reused"]
    definitions = root / "definitions.dl"
    definitions.write_text('to get identity item:\n    @intrinsic("return", item)\n')
    library = parity(entry=definitions, flags=("--no-main",), target=root / "library.ll")
    assert not re.search(rb"^define [^\n]*@main\(", library[2], re.MULTILINE)
    ordinary = parity(entry=definitions, target=root / "definitions.ll")
    assert re.search(rb"^define [^\n]*@main\(", ordinary[2], re.MULTILINE)
    assert ordinary[0]["cache_entry"] != library[0]["cache_entry"]
    assert build()[0]["frontend_reused"]

    failed_output = root / "missing-directory" / "program.ll"
    failed = build(success=False, target=failed_output)
    assert failed[1] == build(False, success=False, target=failed_output)[1]

    warning = root / "warning.dl"
    warning.write_text('integer means: @intrinsic("type", "int")\n'
                       'value means: 1\n'
                       'to show {integer:value}: @intrinsic("discard", value)\nshow 2\n')
    config = root / "config.dl"
    wording = ('dynlex options:\n    messages:\n'
               '        explicit parameter ambiguity: "First warning for {parameter}"\n')
    config.write_text(wording)
    warned = parity(entry=warning)
    assert "First warning for value" in warned[1]
    timestamp = output.stat().st_mtime_ns
    config.write_text(wording.replace("First", "Second"))
    renamed = parity(entry=warning)
    assert renamed[0]["frontend_reused"] and "Second warning for value" in renamed[1]
    assert renamed[0]["llvm_outputs_built"] == 0 and renamed[0]["llvm_outputs_restored"] == 0
    assert renamed[0]["codegen_and_link_ms"] == 0 and output.stat().st_mtime_ns == timestamp


def check_incremental_llvm_debug_output(root, compiler):
    from test_incremental_debug_ir import debug_ir_global_edits

    root.mkdir()
    source = root / "main.dl"
    for optimization in ("-O0", "-O2"):
        cache = root / optimization
        for index, text in enumerate(debug_ir_global_edits()):
            source.write_text(text)
            products = []
            for incremental in (True, False):
                output = root / ("incremental.ll" if incremental else "fresh.ll")
                command = [str(compiler), str(source), "-g", optimization, "--emit-llvm", "-o", str(output)]
                command += (["--cache-dir", str(cache), "--incremental-stats"]
                            if incremental else ["--no-incremental"])
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
                assert result.returncode == 0, result.stderr
                lines = result.stderr.splitlines(keepends=True)
                records = [line for line in lines if line.startswith("incremental: ")]
                if incremental and index == 1:
                    assert json.loads(records[0][13:])["functions_ir_reused"] > 0
                llvm = output.read_bytes().split(b"\n", 1)[1]
                assert b"dynlex.debug.types" not in llvm, "compiler-private debug products leaked into LLVM output"
                products.append(("".join(line for line in lines if not line.startswith("incremental: ")), llvm))
            assert products[0] == products[1], "incremental LLVM debug output differs from clean source"
