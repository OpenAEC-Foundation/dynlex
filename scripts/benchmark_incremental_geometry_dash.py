#!/usr/bin/env python3
"""Compare edited and clean native builds of the Geometry Dash game."""

import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from benchmark_ui_sources import stage_game, ui_repository


project = Path(__file__).resolve().parent.parent
compiler = project / "build" / ("dynlex.exe" if os.name == "nt" else "dynlex")
ui = ui_repository(project)
game = Path("examples/games/geometry_dash")
runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1
cases = {
    "comment_in_physics": (game / "physics.dl", "the dash floor means: 610.0", "the dash floor means: 610.0 # benchmark comment"),
    "physics_constant": (game / "physics.dl", "the dash floor means: 610.0", "the dash floor means: 611.0"),
    "rotation_step": (game / "physics.dl", "add 3.4 to body's rotation", "add 3.5 to body's rotation"),
    "unrelated_pattern": (
        game / "physics.dl",
        "the dash floor means: 610.0\n",
        "the dash floor means: 610.0\n\nto benchmark marker: @intrinsic(\"discard\", 1)\n",
    ),
    "top_level_theme": (game / "main.dl", "UI ink 182 255 58", "UI ink 183 255 58"),
}
if len(sys.argv) > 2:
    cases = {name: case for name, case in cases.items() if name in sys.argv[2:]}
    if len(cases) != len(set(sys.argv[2:])):
        raise SystemExit("unknown benchmark case")


def compile_program(root, incremental):
    output = root / (("incremental" if incremental else "clean") + (".exe" if os.name == "nt" else ""))
    command = [str(compiler), str(game / "main.dl"), "-o", str(output)]
    if incremental:
        command += ["--cache-dir", str(root / "cache"), "--incremental-stats"]
    else:
        command += ["--no-incremental"]
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
            stage_game(project, ui, "geometry_dash", root)
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
                         "resolution_precedence_ms", "resolution_rematch_ms", "resolution_candidate_collection_ms",
                         "resolution_candidate_sort_ms", "resolution_replay_setup_ms",
                         "resolution_candidate_match_ms", "precedence_tree_signature_ms",
                         "precedence_signature_ms", "precedence_rule_signature_ms",
                         "variable_like_count_references_scanned",
                         "variable_like_count_units_reused", "parameter_count_cache_ms", "matcher_declarations_reused",
                         "body_resolution_reference_visits", "matcher_cache_hits",
                         "classification_ms", "classification_units_reused", "classification_parameters_reused",
                         "classification_units_invalidated", "classification_dependency_edges",
                         "classification_candidate_checks", "classification_inputs_ms",
                         "classification_dependencies_ms", "classification_restore_ms", "classification_save_ms",
                         "classification_query_units_built", "classification_query_units_reused",
                         "classification_dependency_units_reused",
                         "cached_body_resolution_references_skipped",
                         "cached_body_selection_ms",
                         "body_references_in_instantiated_functions",
                         "body_references_in_uninstantiated_functions",
                         "body_references_in_flex_functions",
                         "body_references_in_conversion_functions",
                         "body_references_in_other_definitions",
                         "body_resolution_match_cache_hits", "body_resolution_match_computations",
                         "temporary_expression_parse_hits",
                         "constraint_probes_reused", "constraint_probes_run",
                         "constraint_values_reused", "constraint_expressions_reused",
                         "deferred_match_keys_replayed", "precedence_match_keys_replayed",
                         "matcher_key_ms", "matcher_restore_ms", "matcher_snapshot_ms", "matcher_projection_ms",
                         "projection_ms", "projection_setup_ms", "projection_body_ms",
                         "projection_declarations_ms", "projection_matches_ms",
                         "projection_declaration_sections_reused",
                         "projection_inactive_ms", "projection_ambient_ms",
                         "source_projection_mask_ms", "source_projection_files_ms",
                         "preinference_ms", "inference_ms", "ir_generation_ms", "snapshot_ms",
                         "partition_ms", "link_ms", "native_dependency_ms",
                         "native_partition_modules_built", "native_partition_modules_skipped",
                         "native_function_dependencies_reused",
                         "native_function_fingerprints_computed",
                         "dependency_graph_ms", "codegen_and_link_ms",
                         "cache_payload_load_ms", "cache_write_ms")},
        "frontend_reused": all(sample[2]["frontend_reused"] for sample in samples),
        "functions_ir_reused": round(statistics.median(sample[2]["functions_ir_reused"] for sample in samples)),
        "inference_stage_ms": [
            round(statistics.median(sample[2]["inference_stage_ms"][stage] for sample in samples), 1)
            for stage in range(6)
        ],
        "projection_top_level_sections_reused": round(statistics.median(
            sample[2]["projection_top_level_sections_reused"] for sample in samples)),
    }, sort_keys=True))
