#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shlex
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from dl_files import build_import_graph, discover_dl_files, discover_entry_points, reachable_files


ENTRYPOINT_FLAGS_RE = re.compile(r"^\s*#\s*DynLex entrypoint flags:\s*(.*?)\s*$", re.MULTILINE)


@dataclass(frozen=True)
class CompileMode:
    name: str
    arguments: tuple[str, ...]
    extension: str


def is_existing_test_file(path: Path, repo_root: Path) -> bool:
    relative = path.relative_to(repo_root)
    return relative.parts[:2] == ("tests", "required") or relative == Path("tests/purity_dump/main.dl")


def is_syntax_config(path: Path) -> bool:
    return path.name == "config.dl"


def project_syntax_config(path: Path, repo_root: Path) -> Path | None:
    current = path.parent
    while True:
        candidate = (current / "config.dl").resolve()
        if candidate.is_file():
            return candidate
        if current == repo_root or current == current.parent:
            return None
        current = current.parent


def compile_mode(path: Path, import_closure: set[Path], repo_root: Path) -> CompileMode:
    source = path.read_text(encoding="utf-8", errors="replace")
    directives = ENTRYPOINT_FLAGS_RE.findall(source)
    if len(directives) > 1:
        raise ValueError("entry point declares multiple DynLex entrypoint flags directives")
    shader_library = (repo_root / "lib/shader.dl").resolve()
    imports_shader_library = shader_library in import_closure

    if not directives:
        if imports_shader_library:
            raise ValueError("entry points importing lib/shader.dl must declare their shader stage in a compile directive")
        return CompileMode("llvm", ("--emit-llvm",), ".ll")

    try:
        arguments = tuple(shlex.split(directives[0]))
    except ValueError as error:
        raise ValueError(f"invalid DynLex entrypoint flags directive: {error}") from error
    valid_arguments = {
        ("--emit-spirv", "--shader-stage=vertex"),
        ("--emit-spirv", "--shader-stage=fragment"),
    }
    if arguments not in valid_arguments:
        raise ValueError("entrypoint flags must be --emit-spirv followed by --shader-stage=vertex or fragment")
    if not imports_shader_library:
        raise ValueError("SPIR-V entry points must import lib/shader.dl")

    stage = arguments[1].removeprefix("--shader-stage=")
    return CompileMode(f"spirv-{stage}", arguments, ".spv")


def first_failure_line(stderr: str) -> str:
    lines = [line.strip() for line in stderr.splitlines() if line.strip()]
    for line in lines:
        if "Error:" in line:
            return line
    return lines[-1] if lines else "compiler exited without a diagnostic"


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile every DynLex entry point not owned by the required test suite.")
    parser.add_argument("--compiler", default="build/dynlex", help="Compiler path relative to the repository root")
    parser.add_argument("--timeout", type=float, default=30.0, help="Maximum compilation time per entry point")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    compiler = Path(args.compiler)
    if not compiler.is_absolute():
        compiler = (repo_root / compiler).resolve()
    if not compiler.is_file():
        parser.error(f"compiler not found: {compiler}")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    files = discover_dl_files(repo_root)
    config_files = [path for path in files if is_syntax_config(path)]
    source_files = [path for path in files if not is_syntax_config(path)]
    graph, unresolved_imports = build_import_graph(repo_root, source_files)
    if unresolved_imports:
        for importer, token in unresolved_imports:
            print(f"FAIL {importer.relative_to(repo_root)}: unresolved import {token}")
        return 1

    entry_points = discover_entry_points(source_files, graph)
    existing_test_files = [path for path in source_files if is_existing_test_file(path, repo_root)]
    covered = reachable_files(entry_points + existing_test_files, graph)
    uncovered = sorted(set(source_files) - covered)
    if uncovered:
        print("FAIL import graph contains files unreachable from any entry point:")
        for path in uncovered:
            print(f"  {path.relative_to(repo_root)}")
        return 1

    validation_roots = entry_points + existing_test_files
    builtin_config = (repo_root / "lib/config.dl").resolve()
    validated_configs = {builtin_config} if builtin_config in config_files else set()
    validated_configs.update(
        config for root in validation_roots if (config := project_syntax_config(root, repo_root)) is not None
    )
    unvalidated_configs = sorted(set(config_files) - validated_configs)
    if unvalidated_configs:
        print("FAIL syntax configuration files are not selected by any entry point:")
        for path in unvalidated_configs:
            print(f"  {path.relative_to(repo_root)}")
        return 1

    additional_entry_points = [path for path in entry_points if not is_existing_test_file(path, repo_root)]
    failures: list[tuple[Path, str]] = []
    started = time.monotonic()

    with tempfile.TemporaryDirectory(prefix="dynlex-entrypoints-") as temporary_directory:
        output_directory = Path(temporary_directory)
        for index, path in enumerate(additional_entry_points, start=1):
            relative = path.relative_to(repo_root)
            closure = reachable_files([path], graph)
            try:
                mode = compile_mode(path, closure, repo_root)
            except ValueError as error:
                failures.append((path, str(error)))
                print(f"FAIL [{index}/{len(additional_entry_points)}] {relative}: {error}")
                continue

            output = output_directory / f"entry-{index}{mode.extension}"
            command = [str(compiler), str(relative), *mode.arguments, "-o", str(output)]
            entry_started = time.monotonic()
            try:
                result = subprocess.run(
                    command,
                    cwd=repo_root,
                    text=True,
                    capture_output=True,
                    timeout=args.timeout,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                reason = f"timed out after {args.timeout:g}s"
                failures.append((path, reason))
                print(f"FAIL [{index}/{len(additional_entry_points)}] {relative}: {reason}")
                continue

            elapsed = time.monotonic() - entry_started
            if result.returncode != 0:
                reason = first_failure_line(result.stderr)
                failures.append((path, reason))
                print(f"FAIL [{index}/{len(additional_entry_points)}] {relative} ({mode.name}, {elapsed:.2f}s): {reason}")
                continue
            print(f"PASS [{index}/{len(additional_entry_points)}] {relative} ({mode.name}, {elapsed:.2f}s)")

    elapsed = time.monotonic() - started
    print(
        f"Entry-point results: {len(additional_entry_points) - len(failures)} passed, {len(failures)} failed, "
        f"{len(existing_test_files)} files covered by existing tests; "
        f"{len(source_files)} source files reachable and {len(config_files)} syntax configs selected ({elapsed:.2f}s)"
    )
    if failures:
        print("Failures:")
        for path, reason in failures:
            print(f"  {path.relative_to(repo_root)}: {reason}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
