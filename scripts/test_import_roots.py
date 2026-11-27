#!/usr/bin/env python3
"""Import resolution must keep one import graph on one resolution root.

A file resolved from the compiler's library tree resolves its own imports
under that same tree, even when the working directory contains files with the
same relative paths. Without root inheritance, the standard library's nested
imports fall back to the working directory and mix trees.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <compiler>")

COMPILER = Path(sys.argv.pop()).resolve()


class ImportRootConsistencyTests(unittest.TestCase):
    def copy_compiler(self, prefix: Path) -> Path:
        binary = prefix / "bin" / COMPILER.name
        binary.parent.mkdir(parents=True)
        shutil.copy2(COMPILER, binary)
        for library in COMPILER.parent.glob("*.dll"):
            shutil.copy2(library, binary.parent / library.name)
        return binary

    def test_relocated_compiler_uses_its_library_and_syntax_root(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-relocated-imports-") as directory:
            root = Path(directory)
            prefix = root / "installation with spaces"
            compiler = self.copy_compiler(prefix)
            library = prefix / "share/dynlex/lib"
            library.mkdir(parents=True)
            builtin_config = library / "config.dl"
            builtin_config.write_text('dynlex options:\n    import: "include"\n')
            (library / "entry.dl").write_text("include lib/helper.dl\n")
            helper = library / "helper.dl"
            helper.write_text("probe means: 42\n")
            project = root / "project"
            project.mkdir()
            project_config = project / "config.dl"
            project_config.write_text('dynlex options:\n    import: "use"\n')
            working = root / "unrelated"
            (working / "lib").mkdir(parents=True)
            (working / "lib/helper.dl").write_text("wrong working-directory source\n")
            main = project / "main.dl"
            for request in ("entry.dl", "lib/entry.dl"):
                with self.subTest(request=request):
                    main.write_text("use " + request + "\n")
                    result = subprocess.run(
                        [str(compiler), str(main), "--dump-dependencies"], cwd=working,
                        capture_output=True, text=True, timeout=10,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    report = json.loads(result.stdout)
                    self.assertTrue(report["complete"])
                    self.assertEqual({Path(path).resolve() for path in report["sources"]},
                                     {main.resolve(), (library / "entry.dl").resolve(), helper.resolve()})
                    self.assertEqual({Path(path).resolve() for path in report["configurations"]},
                                     {builtin_config.resolve(), project_config.resolve()})

    def test_relocated_compiler_does_not_use_the_build_checkouts_libraries(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-incomplete-installation-") as directory:
            root = Path(directory)
            compiler = self.copy_compiler(root / "installation")
            main = root / "main.dl"
            main.write_text("import std.dl\n")
            result = subprocess.run(
                [str(compiler), str(main), "--dump-dependencies"], cwd=root,
                capture_output=True, text=True, timeout=10,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(json.loads(result.stdout)["complete"])
            self.assertIn("failed to import source file: std.dl", result.stderr)

    def test_indented_configured_import_is_loaded(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-indented-import-") as directory:
            root = Path(directory)
            (root / "config.dl").write_text('dynlex options:\n    import: "include"\n')
            main = root / "main.dl"
            body = root / "body.dl"
            main.write_text('function [run]:\n    execute:\n        include body.dl\n')
            body.write_text('        @intrinsic("discard", 0)\n')
            result = subprocess.run([str(COMPILER), str(main), "--dump-dependencies"],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertTrue(report["complete"])
            self.assertIn(body.resolve(), {Path(source).resolve() for source in report["sources"]})
            body.unlink()
            missing = subprocess.run([str(COMPILER), str(main), "--dump-dependencies"],
                                     cwd=root, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(missing.returncode, 0)
            self.assertFalse(json.loads(missing.stdout)["complete"])
            self.assertIn("main.dl:3:16-24:", missing.stderr)

    def test_project_syntax_and_symlinked_builtin_syntax(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-source-syntax-") as directory:
            root = Path(directory)
            (root / "config.dl").write_text(
                'dynlex options:\n    import: "importeer"\n    function: "functie"\n'
                '    child sections:\n        execute: "doe"\n', encoding="utf-8"
            )
            (root / "lib").mkdir()
            # A project directory called lib is still project code. A symlink
            # into the compiler library must retain the builtin syntax instead.
            (root / "lib" / "helper.dl").write_text(
                'functie say hello:\n    doe:\n        print line "ok"\n', encoding="utf-8"
            )
            try:
                library = Path(__file__).resolve().parent.parent / "lib"
                (root / "builtin.dl").symlink_to(library / "std.dl")
            except OSError as error:
                self.skipTest(f"source symlinks unavailable: {error}")
            main = root / "main.dl"
            main.write_text(
                'importeer builtin.dl\nimporteer lib/helper.dl\nsay hello\n', encoding="utf-8"
            )
            output = root / "main.out"
            result = subprocess.run(
                [str(COMPILER), str(main), "--no-incremental", "-o", str(output)], cwd=root,
                capture_output=True, text=True, timeout=120
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout, "ok\n")

    def test_nested_library_imports_ignore_working_directory(self) -> None:
        self.assertTrue(COMPILER.exists(), f"compiler not built at {COMPILER}")
        with tempfile.TemporaryDirectory(prefix="dynlex-import-roots-") as temporary_directory:
            root = Path(temporary_directory)
            main = root / "main.dl"
            main.write_text('import std.dl\nprint line "ok"\n', encoding="utf-8")

            # A working directory whose lib/string.dl would break the build if
            # the standard library's nested `import lib/string.dl` fell back to
            # the working directory instead of staying on the resolved root.
            work = root / "work"
            (work / "lib").mkdir(parents=True)
            (work / "lib" / "string.dl").write_text("this is not valid dynlex $$$\n", encoding="utf-8")

            output = root / "main.out"
            compileResult = subprocess.run(
                [str(COMPILER), str(main), "--no-incremental", "-o", str(output)],
                cwd=work,
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertEqual(
                compileResult.returncode, 0,
                f"compile failed, likely mixed import roots:\n{compileResult.stdout}\n{compileResult.stderr}",
            )
            runResult = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
            self.assertEqual(runResult.returncode, 0)
            self.assertEqual(runResult.stdout.strip(), "ok")

    def test_main_file_seeds_import_root_outside_working_directory(self) -> None:
        self.assertTrue(COMPILER.exists(), f"compiler not built at {COMPILER}")
        with tempfile.TemporaryDirectory(prefix="dynlex-main-import-root-") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "project"
            (project / "pkg").mkdir(parents=True)
            main = project / "main.dl"
            main.write_text("import pkg/entry.dl\n", encoding="utf-8")
            (project / "pkg" / "entry.dl").write_text(
                "import pkg/nested.dl\n", encoding="utf-8"
            )
            (project / "pkg" / "nested.dl").write_text("", encoding="utf-8")

            work = root / "unrelated-working-directory"
            (work / "pkg").mkdir(parents=True)
            (work / "pkg" / "nested.dl").write_text(
                "this conflicting working-directory file must not be imported $$$\n", encoding="utf-8"
            )
            output = root / "main.out"
            compileResult = subprocess.run(
                [str(COMPILER), str(main), "--no-incremental", "-o", str(output)],
                cwd=work,
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertEqual(
                compileResult.returncode, 0,
                f"nested project import escaped the main file's root:\n{compileResult.stdout}\n{compileResult.stderr}",
            )


if __name__ == "__main__":
    unittest.main()
