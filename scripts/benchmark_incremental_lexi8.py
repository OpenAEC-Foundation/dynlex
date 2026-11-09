#!/usr/bin/env python3
"""Measure edited native builds of the headless LEXI-8 Breakout program."""

import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


project = Path(__file__).resolve().parent.parent
compiler = project / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex")
source_dir = project / "tests" / "games" / "lexi8"
runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1
cases = {
    "comment_in_machine": ("chip8.dl", "# Benchmark comment; executable code is unchanged.\n", None),
    "unused_string_helper": ("lib/string.dl", "to get {string:value} is invalid:\n    if value's length < 0:",
                             "to get {string:value} is invalid:\n    if value's length < -1:"),
    "frame_count_in_main": ("test.dl", "run 20 frames", "run 21 frames"),
    "lives_in_breakout": ("breakout.dl", "load value 3 into register registers's lives",
                           "load value 2 into register registers's lives"),
}


def compile_program(root, incremental):
    output = root / (("incremental" if incremental else "clean") + (".exe" if os.name == "nt" else ""))
    command = [str(compiler), "tests/games/lexi8/test.dl", "-o", str(output)]
    if incremental:
        command += ["--incremental", str(root / "cache"), "--incremental-stats"]
    started = time.perf_counter()
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
    elapsed_ms = (time.perf_counter() - started) * 1000
    statistics_line = [line for line in result.stderr.splitlines() if line.startswith("incremental: ")]
    stats = json.loads(statistics_line[0][len("incremental: "):]) if statistics_line else None
    diagnostics = "\n".join(line for line in result.stderr.splitlines() if not line.startswith("incremental: "))
    if result.returncode:
        raise RuntimeError(f"compiler failed: {command}\n{diagnostics}")
    execution = subprocess.run([str(output)], cwd=root, capture_output=True, timeout=30)
    if execution.returncode:
        raise RuntimeError(f"program failed: {output}\n{execution.stderr.decode(errors='replace')}")
    return elapsed_ms, stats, diagnostics, execution.stdout


results = {name: [] for name in cases}
with tempfile.TemporaryDirectory(prefix="dynlex-lexi8-benchmark-") as temporary:
    for repetition in range(runs):
        for name, (filename, before, after) in cases.items():
            root = Path(temporary) / f"{name}-{repetition}"
            program = root / "tests" / "games" / "lexi8"
            program.mkdir(parents=True)
            shutil.copytree(project / "lib", root / "lib")
            for source in source_dir.glob("*.dl"):
                (program / source.name).write_bytes(source.read_bytes())
            compile_program(root, True)
            changed = root / filename if filename.startswith("lib/") else program / filename
            original = changed.read_text()
            if after is None:
                changed.write_text(before + original)
            else:
                if original.count(before) != 1:
                    raise RuntimeError(f"expected one edit site in {changed}")
                changed.write_text(original.replace(before, after))
            incremental_ms, stats, incremental_diagnostics, incremental_output = compile_program(root, True)
            clean_ms, _, clean_diagnostics, clean_output = compile_program(root, False)
            if (incremental_diagnostics, incremental_output) != (clean_diagnostics, clean_output):
                raise RuntimeError(f"incremental output differs from clean build: {name}")
            results[name].append((incremental_ms, clean_ms, stats))
            print(f"{name} run {repetition + 1}: incremental={incremental_ms:.1f} ms clean={clean_ms:.1f} ms", flush=True)

for name, samples in results.items():
    print(json.dumps({
        "case": name,
        "runs": len(samples),
        "incremental_wall_ms": round(statistics.median(sample[0] for sample in samples), 1),
        "clean_wall_ms": round(statistics.median(sample[1] for sample in samples), 1),
        "frontend_ms": round(statistics.median(sample[2]["frontend_ms"] for sample in samples), 1),
        "import_ms": round(statistics.median(sample[2]["import_ms"] for sample in samples), 1),
        "parse_ms": round(statistics.median(sample[2]["parse_ms"] for sample in samples), 1),
        "resolve_ms": round(statistics.median(sample[2]["resolve_ms"] for sample in samples), 1),
        "validate_target_ms": round(statistics.median(sample[2]["validate_target_ms"] for sample in samples), 1),
        "cache_load_ms": round(statistics.median(sample[2]["cache_load_ms"] for sample in samples), 1),
        "projection_ms": round(statistics.median(sample[2]["projection_ms"] for sample in samples), 1),
        "projection_setup_ms": round(statistics.median(sample[2]["projection_setup_ms"] for sample in samples), 1),
        "projection_body_ms": round(statistics.median(sample[2]["projection_body_ms"] for sample in samples), 1),
        "projection_declarations_ms": round(statistics.median(sample[2]["projection_declarations_ms"] for sample in samples), 1),
        "projection_matches_ms": round(statistics.median(sample[2]["projection_matches_ms"] for sample in samples), 1),
        "projection_global_matches_reused": round(statistics.median(sample[2]["projection_global_matches_reused"] for sample in samples)),
        "projection_match_entries_reused": round(statistics.median(sample[2]["projection_match_entries_reused"] for sample in samples)),
        "projection_ambient_ms": round(statistics.median(sample[2]["projection_ambient_ms"] for sample in samples), 1),
        "inactive_projection_ms": round(statistics.median(sample[2]["inactive_projection_ms"] for sample in samples), 1),
        "preinference_ms": round(statistics.median(sample[2]["preinference_ms"] for sample in samples), 1),
        "inference_ms": round(statistics.median(sample[2]["inference_ms"] for sample in samples), 1),
        "snapshot_ms": round(statistics.median(sample[2]["snapshot_ms"] for sample in samples), 1),
        "codegen_and_link_ms": round(statistics.median(sample[2]["codegen_and_link_ms"] for sample in samples), 1),
        "cache_write_ms": round(statistics.median(sample[2]["cache_write_ms"] for sample in samples), 1),
        "type_inference_reused": all(sample[2]["type_inference_reused"] for sample in samples),
        "diagnostic_records_reused": round(statistics.median(sample[2]["diagnostic_records_reused"] for sample in samples)),
        "inactive_projection_matches_rechecked": round(statistics.median(sample[2]["inactive_projection_matches_rechecked"] for sample in samples)),
        "inference_instantiations_reused": round(statistics.median(sample[2]["inference_instantiations_reused"] for sample in samples)),
        "pure_value_summary_hits": round(statistics.median(sample[2]["pure_value_summary_hits"] for sample in samples)),
        "functions_ir_reused": round(statistics.median(sample[2]["functions_ir_reused"] for sample in samples)),
        "body_resolution_reference_visits": round(statistics.median(sample[2]["body_resolution_reference_visits"] for sample in samples)),
        "matcher_cache_hits": round(statistics.median(sample[2]["matcher_cache_hits"] for sample in samples)),
        "deferred_match_keys_replayed": round(statistics.median(sample[2]["deferred_match_keys_replayed"] for sample in samples)),
        "matcher_key_ms": round(statistics.median(sample[2]["matcher_key_ms"] for sample in samples), 1),
        "matcher_restore_ms": round(statistics.median(sample[2]["matcher_restore_ms"] for sample in samples), 1),
        "expression_parse_hits": round(statistics.median(sample[2]["expression_parse_hits"] for sample in samples)),
        "expression_parse_misses": round(statistics.median(sample[2]["expression_parse_misses"] for sample in samples)),
    }, sort_keys=True))
