#!/usr/bin/env python3
from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from test_entrypoints import (
    DependencyInspectionError,
    check_entry_point,
    expected_dependency_failure,
    read_dependencies,
    shader_entry_points,
)


PROJECT = Path(__file__).resolve().parent.parent
COMPILER = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else PROJECT / "build/dynlex"


class EntryPointCheckerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="dynlex-entrypoint-checker-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()

    def source(self, content: str) -> Path:
        path = self.root / "main.dl"
        path.write_text(content, encoding="utf-8")
        return path

    def check(self, content: str):
        return check_entry_point(COMPILER, self.source(content), PROJECT, self.root, 15)

    def test_dependency_data_uses_configured_imports_even_in_invalid_programs(self) -> None:
        config = self.root / "config.dl"
        config.write_text('dynlex options:\n    import: "bring"\n', encoding="utf-8")
        helper = self.root / "helper.dl"
        helper.write_text("", encoding="utf-8")
        main = self.source("unsupported ☃\nbring helper.dl\n")
        data = read_dependencies(COMPILER, main, PROJECT, 15)
        self.assertEqual(data.sources, {main, helper})
        self.assertIn(config, data.configurations)
        self.assertIn(PROJECT / "lib/config.dl", data.configurations)

    def test_missing_import_is_an_inspection_failure(self) -> None:
        main = self.source("import missing.dl\n")
        with self.assertRaisesRegex(RuntimeError, "failed to import"):
            read_dependencies(COMPILER, main, PROJECT, 15)

    def test_invalid_configuration_preserves_its_selected_path(self) -> None:
        config = self.root / "config.dl"
        config.write_text('dynlex options:\n    unknown option: "value"\n', encoding="utf-8")
        with self.assertRaises(DependencyInspectionError) as failure:
            read_dependencies(COMPILER, self.source(""), PROJECT, 15)
        self.assertIn(config, failure.exception.configurations)

    def test_only_exact_declared_diagnostic_failures_are_expected(self) -> None:
        fixture = self.root / "tests/required/invalid_config"
        fixture.mkdir(parents=True)
        source = fixture / "main.dl"
        source.write_text("", encoding="utf-8")
        (fixture / "expected_diagnostics.txt").write_text("expected error\n", encoding="utf-8")
        self.assertTrue(expected_dependency_failure(source, self.root, "expected error\n"))
        self.assertFalse(expected_dependency_failure(source, self.root, "different error\n"))
        self.assertFalse(expected_dependency_failure(self.root / "other.dl", self.root, "expected error\n"))

    def test_native_program_does_not_inherit_unused_shader_library_requirements(self) -> None:
        result = self.check('import lib/shader.dl\n@intrinsic("return", 0)\n')
        self.assertTrue(result.succeeded, result.failures)
        self.assertIn("llvm", result.accepted_modes)

    def test_shader_without_library_or_directive_is_discovered_through_its_output(self) -> None:
        result = self.check('''function send pixel:
    replacement:
        @intrinsic("shader output", @intrinsic("shader input", "FragCoord"))
send pixel
''')
        self.assertTrue(result.succeeded, result.failures)
        self.assertEqual(result.accepted_modes, ["spirv-fragment"])

    def test_dual_stage_program_checks_both_compiled_stages(self) -> None:
        result = self.check('''# DynLex entrypoint flags: --emit-spirv --shader-stage=compute
flex section when condition:
    replacement:
        @intrinsic("if", condition)
when @intrinsic("shader stage is", "vertex"):
    @intrinsic("shader output", @intrinsic("shader input", "Position"))
when @intrinsic("shader stage is", "fragment"):
    @intrinsic("shader output", @intrinsic("shader input", "FragCoord"))
''')
        self.assertTrue(result.succeeded, result.failures)
        self.assertIn("spirv-vertex", result.accepted_modes)
        self.assertIn("spirv-fragment", result.accepted_modes)
        self.assertEqual(len(result.attempts), 4)

    def test_invalid_program_retains_every_compilers_diagnostic(self) -> None:
        result = self.check('@intrinsic("unknown operation")\n')
        self.assertFalse(result.succeeded)
        self.assertEqual(len(result.failures), 4)
        self.assertTrue(all("unknown intrinsic" in failure for failure in result.failures))

    def test_shader_execution_models_are_read_from_binary_entry_points(self) -> None:
        output = self.root / "actual.spv"
        words = [0x07230203, 0x00010000, 0, 2, 0, (5 << 16) | 15, 4, 1, 0x6E69616D, 0]
        output.write_bytes(struct.pack(f"<{len(words)}I", *words))
        self.assertEqual(shader_entry_points(output), [(4, "main")])

    def test_broken_compiler_output_is_a_failure(self) -> None:
        output = self.root / "broken.spv"
        output.write_bytes(struct.pack("<6I", 0x07230203, 0x00010000, 0, 2, 0, 15))
        with self.assertRaisesRegex(RuntimeError, "instruction"):
            shader_entry_points(output)

    def test_a_successful_mode_does_not_hide_an_incorrect_compiled_stage(self) -> None:
        with patch("test_entrypoints.shader_entry_points", return_value=[(4, "main")]):
            result = self.check('@intrinsic("return", 0)\n')
        self.assertIn("llvm", result.accepted_modes)
        self.assertFalse(result.succeeded)
        self.assertTrue(any(attempt.fatal for attempt in result.attempts))

    def test_a_successful_native_mode_does_not_hide_invalid_shader_output(self) -> None:
        actual_run = subprocess.run

        def reject_shader(command, **kwargs):
            if command[0] == "spirv-val":
                return subprocess.CompletedProcess(command, 1, "", "invalid shader interface")
            return actual_run(command, **kwargs)

        with patch("test_entrypoints.subprocess.run", side_effect=reject_shader):
            result = self.check('@intrinsic("return", 0)\n')
        self.assertEqual(result.accepted_modes, ["llvm"])
        self.assertFalse(result.succeeded)
        self.assertTrue(all(attempt.fatal for attempt in result.attempts[1:]))
        self.assertTrue(all("invalid shader interface" in attempt.diagnostics for attempt in result.attempts[1:]))

    def test_dependency_report_is_separate_from_codegen_modes(self) -> None:
        result = subprocess.run(
            [str(COMPILER), str(self.source("")), "--dump-dependencies", "--emit-llvm"],
            cwd=PROJECT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--dump-dependencies", result.stderr)
        self.assertFalse((self.root / "main.dl.ll").exists())


if __name__ == "__main__":
    unittest.main()
