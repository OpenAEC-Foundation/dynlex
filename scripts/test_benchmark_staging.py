#!/usr/bin/env python3
"""Check independent compiler staging and the images actually benchmarked."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


PROJECT = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]


class CompilerStagingTests(unittest.TestCase):
    def run_comparison(self, directory, copy_mode="copy", mutate=False):
        spec = importlib.util.spec_from_file_location(
            "benchmark_staging_fixture", PROJECT / "scripts" / "benchmark_incremental_comparison.py")
        benchmark = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(benchmark)
        benchmark.REPOSITORY = directory
        originals = [directory / name for name in ("first", "second")]
        for path in originals:
            path.write_bytes(b"compiler image")
        output = directory / "result.json"
        copy_file = shutil.copy2

        def copy(source, destination, *args, **kwargs):
            if copy_mode == "link":
                os.link(source, destination)
            else:
                copy_file(source, destination, *args, **kwargs)
                if copy_mode == "consume":
                    Path(source).unlink()
            return destination

        def run(command, **_):
            if "--version" not in command:
                Path(command[command.index("-o") + 1]).write_bytes(b"program image")
                if mutate:
                    Path(command[0]).write_bytes(b"changed compiler image")
            return subprocess.CompletedProcess(command, 0, "fixture version\n", "")

        def application(root, _revision):
            for name in ("tests/games/lexi8/main.dl", "tests/games/lexi8/chip8.dl",
                         "tests/games/lexi8/breakout.dl", "lib/string.dl", "lib/graphics.dl"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("")

        arguments = ["benchmark", "--baseline", str(originals[0]), "--candidate", str(originals[1]),
                     "--output", str(output), "--cases", "cold", "--cold-repetitions", "1"]
        with patch.object(sys, "argv", arguments), \
                patch.object(benchmark.subprocess, "check_output", return_value="fixture-revision\n"), \
                patch.object(benchmark.subprocess, "run", side_effect=run), \
                patch.object(benchmark, "child_cpu_seconds", side_effect=iter(range(100))), \
                patch.object(benchmark.shutil, "copy2", side_effect=copy), \
                patch.object(benchmark, "stage_application", side_effect=application), \
                contextlib.redirect_stdout(io.StringIO()):
            benchmark.main()
        return json.loads(output.read_text()), originals

    def test_hard_links_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaisesRegex(RuntimeError, "independent"):
                self.run_comparison(Path(name), copy_mode="link")

    def test_consumed_inputs_use_staged_images(self):
        with tempfile.TemporaryDirectory() as name:
            report, originals = self.run_comparison(Path(name), copy_mode="consume")
            self.assertTrue(report["complete"] and report["compiler_unchanged"])
            self.assertTrue(all(not path.exists() for path in originals))

    def test_staged_mutation_detected(self):
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaisesRegex(RuntimeError, "changed"):
                self.run_comparison(Path(name), mutate=True)


if __name__ == "__main__":
    unittest.main()
