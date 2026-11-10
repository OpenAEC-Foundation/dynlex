#!/usr/bin/env python3
"""Compare edited and clean native builds of the Geometry Dash game."""

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
game = Path("tests/games/geometry_dash")
runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1
cases = {
    "comment_in_physics": (game / "physics.dl", "return 84", "return 84 # benchmark comment"),
    "physics_constant": (game / "physics.dl", "return 84", "return 85"),
    "top_level_seed": (game / "main.dl", "fill plan using seed 1337", "fill plan using seed 1338"),
    "project_warning_message": (
        Path("config.dl"),
        "Expression {expression} has choices: {chosen} or {alternative}.",
        "New grouping wording for {expression}: {chosen} or {alternative}.",
    ),
}
if len(sys.argv) > 2:
    cases = {name: case for name, case in cases.items() if name in sys.argv[2:]}
    if len(cases) != len(set(sys.argv[2:])):
        raise SystemExit("unknown benchmark case")


def compile_program(root, incremental):
    output = root / (("incremental" if incremental else "clean") + (".exe" if os.name == "nt" else ""))
    command = [str(compiler), str(game / "main.dl"), "-o", str(output)]
    if incremental:
        command += ["--incremental", str(root / "cache"), "--incremental-stats"]
    started = time.perf_counter()
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=30)
    elapsed_ms = (time.perf_counter() - started) * 1000
    stat_lines = [line for line in result.stderr.splitlines() if line.startswith("incremental: ")]
    stats = json.loads(stat_lines[0][len("incremental: "):]) if stat_lines else None
    diagnostics = "".join(line for line in result.stderr.splitlines(keepends=True)
                          if not line.startswith("incremental: "))
    if result.returncode:
        raise RuntimeError(f"compiler failed: {command}\n{diagnostics}")
    return elapsed_ms, stats, diagnostics


results = {name: [] for name in cases}
with tempfile.TemporaryDirectory(prefix="dynlex-geometry-dash-benchmark-") as temporary:
    for repetition in range(runs):
        for name, (filename, before, after) in cases.items():
            root = Path(temporary) / f"{name}-{repetition}"
            shutil.copytree(project / "lib", root / "lib")
            shutil.copytree(project / game, root / game)
            if filename == Path("config.dl"):
                (root / filename).write_text(
                    f'dynlex options:\n    messages:\n        ambiguous operand grouping: "{before}"\n'
                )
            compile_program(root, True)
            changed = root / filename
            original = changed.read_text()
            if original.count(before) != 1:
                raise RuntimeError(f"expected one edit site in {changed}")
            changed.write_text(original.replace(before, after))
            incremental_ms, stats, incremental_diagnostics = compile_program(root, True)
            clean_ms, _, clean_diagnostics = compile_program(root, False)
            if incremental_diagnostics != clean_diagnostics:
                raise RuntimeError(f"incremental diagnostics differ from clean build: {name}")
            if filename == Path("config.dl"):
                if after.split(" for ")[0] not in incremental_diagnostics:
                    raise RuntimeError("edited warning wording did not appear")
                if not stats["frontend_reused"] or stats["codegen_and_link_ms"] or stats["objects_built"]:
                    raise RuntimeError("warning wording edit recompiled executable code")
            results[name].append((incremental_ms, clean_ms, stats))
            print(f"{name} run {repetition + 1}: incremental={incremental_ms:.1f} ms clean={clean_ms:.1f} ms", flush=True)

for name, samples in results.items():
    print(json.dumps({
        "case": name,
        "runs": len(samples),
        "incremental_wall_ms": round(statistics.median(sample[0] for sample in samples), 1),
        "clean_wall_ms": round(statistics.median(sample[1] for sample in samples), 1),
        **{field: round(statistics.median(sample[2][field] for sample in samples), 1)
           for field in ("frontend_ms", "import_ms", "parse_ms", "resolve_ms", "resolution_setup_ms",
                         "resolution_definition_ms", "resolution_body_ms",
                         "resolution_precedence_ms", "resolution_rematch_ms",
                         "variable_like_count_references_scanned",
                         "body_resolution_reference_visits", "matcher_cache_hits",
                         "matcher_key_ms", "matcher_restore_ms", "matcher_snapshot_ms",
                         "projection_ms", "projection_setup_ms", "projection_body_ms",
                         "projection_declarations_ms", "projection_matches_ms",
                         "projection_inactive_ms", "projection_ambient_ms",
                         "preinference_ms", "inference_ms", "snapshot_ms",
                         "dependency_graph_ms", "codegen_and_link_ms",
                         "cache_payload_load_ms", "cache_write_ms")},
        "frontend_reused": all(sample[2]["frontend_reused"] for sample in samples),
        "functions_ir_reused": round(statistics.median(sample[2]["functions_ir_reused"] for sample in samples)),
        "projection_top_level_sections_reused": round(statistics.median(
            sample[2]["projection_top_level_sections_reused"] for sample in samples)),
    }, sort_keys=True))
