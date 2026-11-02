#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from diagnostic_expectations import normalize_diagnostics
from dl_files import discover_dl_files


@dataclass(frozen=True)
class CompileMode:
    name: str
    arguments: tuple[str, ...]
    extension: str
    execution_model: int | None = None


COMPILE_MODES = (
    CompileMode("llvm", ("--emit-llvm",), ".ll"),
    CompileMode("spirv-vertex", ("--emit-spirv", "--shader-stage=vertex"), ".spv", 0),
    CompileMode("spirv-fragment", ("--emit-spirv", "--shader-stage=fragment"), ".spv", 4),
    CompileMode("spirv-compute", ("--emit-spirv", "--shader-stage=compute"), ".spv", 5),
)


@dataclass
class Dependencies:
    sources: set[Path]
    configurations: set[Path]


class DependencyInspectionError(RuntimeError):
    def __init__(self, path: Path, diagnostics: str, configurations: set[Path]):
        super().__init__(f"failed to import dependencies for {path}: {diagnostics}")
        self.diagnostics = diagnostics
        self.configurations = configurations


@dataclass
class CompileAttempt:
    mode: str
    succeeded: bool
    diagnostics: str
    seconds: float
    entry_points: list[tuple[int, str]]
    fatal: bool = False


@dataclass
class EntryPointResult:
    attempts: list[CompileAttempt]

    @property
    def accepted_modes(self) -> list[str]:
        return [attempt.mode for attempt in self.attempts if attempt.succeeded]

    @property
    def succeeded(self) -> bool:
        return bool(self.accepted_modes) and not any(attempt.fatal for attempt in self.attempts)

    @property
    def failures(self) -> list[str]:
        return [f"{attempt.mode}: {attempt.diagnostics}" for attempt in self.attempts if not attempt.succeeded]


def is_existing_test_file(path: Path, repo_root: Path) -> bool:
    relative = path.relative_to(repo_root)
    return relative.parts[:2] == ("tests", "required") or relative == Path("tests/purity_dump/main.dl")


def read_dependencies(compiler: Path, path: Path, repo_root: Path, timeout: float) -> Dependencies:
    result = subprocess.run(
        [str(compiler), str(path), "--dump-dependencies"],
        cwd=repo_root,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(f"dependency inspection exited with status {result.returncode}: {result.stderr.strip()}")
    data = json.loads(result.stdout)
    configurations = {Path(configuration).resolve() for configuration in data["configurations"]}
    if result.returncode != 0:
        raise DependencyInspectionError(path, result.stderr.strip(), configurations)
    if not data["complete"]:
        raise RuntimeError("compiler reported successful dependency inspection with an incomplete graph")
    return Dependencies(
        {Path(source).resolve() for source in data["sources"]},
        configurations,
    )


def expected_dependency_failure(path: Path, repo_root: Path, diagnostics: str) -> bool:
    if not is_existing_test_file(path, repo_root):
        return False
    relative = path.relative_to(repo_root)
    fixture = repo_root.joinpath(*relative.parts[:3])
    expected = fixture / "expected_diagnostics.txt"
    return expected.is_file() and normalize_diagnostics(diagnostics, repo_root) == normalize_diagnostics(
        expected.read_text(encoding="utf-8"), repo_root, reject_line_numbers=True
    )


def shader_entry_points(path: Path) -> list[tuple[int, str]]:
    binary = path.read_bytes()
    if len(binary) < 20 or len(binary) % 4 != 0:
        raise RuntimeError("compiler emitted an invalid SPIR-V header")
    words = struct.unpack(f"<{len(binary) // 4}I", binary)
    if words[0] != 0x07230203:
        raise RuntimeError("compiler emitted an invalid SPIR-V magic number")
    result: list[tuple[int, str]] = []
    position = 5
    while position < len(words):
        count, opcode = words[position] >> 16, words[position] & 0xFFFF
        if count == 0 or position + count > len(words):
            raise RuntimeError("compiler emitted an invalid SPIR-V instruction")
        if opcode == 15:  # OpEntryPoint: execution model, function ID, null-terminated name, interfaces.
            if count < 4:
                raise RuntimeError("compiler emitted an incomplete SPIR-V entry point")
            encoded_name = binary[(position + 3) * 4 : (position + count) * 4]
            terminator = encoded_name.find(b"\0")
            if terminator < 0:
                raise RuntimeError("compiler emitted an unterminated SPIR-V entry-point name")
            name = encoded_name[:terminator].decode("utf-8")
            result.append((words[position + 1], name))
        position += count
    return result


def check_entry_point(
    compiler: Path, path: Path, repo_root: Path, output_directory: Path, timeout: float
) -> EntryPointResult:
    attempts: list[CompileAttempt] = []
    output_directory.mkdir(parents=True, exist_ok=True)
    for mode in COMPILE_MODES:
        output = output_directory / f"{mode.name}{mode.extension}"
        started = time.monotonic()
        entry_points: list[tuple[int, str]] = []
        fatal = False
        try:
            result = subprocess.run(
                [str(compiler), str(path), *mode.arguments, "-o", str(output)],
                cwd=repo_root,
                text=True,
                capture_output=True,
                timeout=timeout,
                check=False,
            )
            succeeded = result.returncode == 0
            diagnostics = result.stderr.strip()
            if result.returncode not in (0, 1):
                fatal = True
                diagnostics = f"compiler exited with status {result.returncode}\n{diagnostics}".strip()
            if succeeded:
                if output.stat().st_size == 0:
                    raise RuntimeError("compiler succeeded without emitting a nonempty output")
                if mode.execution_model is not None:
                    entry_points = shader_entry_points(output)
                    if entry_points != [(mode.execution_model, "main")]:
                        raise RuntimeError(f"unexpected compiled shader entry points: {entry_points}")
                    validation = subprocess.run(
                        ["spirv-val", "--target-env", "vulkan1.3", str(output)],
                        cwd=repo_root, text=True, capture_output=True, timeout=timeout, check=False,
                    )
                    if validation.returncode != 0:
                        raise RuntimeError(
                            f"emitted SPIR-V failed validation (status {validation.returncode}):\n"
                            f"{validation.stdout}{validation.stderr}".strip()
                        )
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            succeeded = False
            fatal = True
            diagnostics = str(error)
        attempts.append(CompileAttempt(mode.name, succeeded, diagnostics, time.monotonic() - started, entry_points, fatal))
    return EntryPointResult(attempts)


def first_failure_line(diagnostics: str) -> str:
    lines = [line.strip() for line in diagnostics.splitlines() if line.strip()]
    return next((line for line in lines if "Error:" in line), lines[-1] if lines else "compiler rejected this target")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate entry points using compiler dependencies and emitted outputs.")
    parser.add_argument("--compiler", default="build/dynlex", help="Compiler path relative to the repository root")
    parser.add_argument("--timeout", type=float, default=30.0, help="Maximum time for each compiler invocation")
    parser.add_argument("--report", type=Path, help="Save every target's result and complete diagnostics as JSON")
    args = parser.parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    compiler = (repo_root / args.compiler).resolve()
    if not compiler.is_file():
        parser.error(f"compiler not found: {compiler}")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    started = time.monotonic()
    files = discover_dl_files(repo_root)
    config_files = {path for path in files if path.name == "config.dl"}
    source_files = set(files) - config_files
    dependencies: dict[Path, Dependencies] = {}
    expected_rejections: dict[Path, set[Path]] = {}
    print(f"Inspecting compiler dependency data for {len(source_files)} source files...", flush=True)
    for path in sorted(source_files):
        try:
            data = read_dependencies(compiler, path, repo_root, args.timeout)
        except DependencyInspectionError as error:
            if not expected_dependency_failure(path, repo_root, error.diagnostics):
                print(f"FAIL {path.relative_to(repo_root)}: {error}")
                return 1
            expected_rejections[path] = error.configurations
            print(f"PASS expected dependency diagnostic: {path.relative_to(repo_root)}", flush=True)
            continue
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"FAIL {path.relative_to(repo_root)}: {error}")
            return 1
        external = data.sources - source_files
        if external:
            print(f"FAIL {path.relative_to(repo_root)}: compiler imported files outside the discovered source tree: {external}")
            return 1
        dependencies[path] = data

    imported = {source for path, data in dependencies.items() for source in data.sources if source != path}
    entry_points = sorted(source_files - imported - expected_rejections.keys())
    existing_test_files = [path for path in sorted(source_files) if is_existing_test_file(path, repo_root)]
    validation_roots = (set(entry_points) | set(existing_test_files)) & dependencies.keys()
    covered = {source for root in validation_roots for source in dependencies[root].sources} | expected_rejections.keys()
    uncovered = source_files - covered
    if uncovered:
        print("FAIL compiler dependency graph contains files unreachable from any entry point:")
        for path in sorted(uncovered):
            print(f"  {path.relative_to(repo_root)}")
        return 1
    validated_configs = {configuration for root in validation_roots for configuration in dependencies[root].configurations}
    validated_configs.update(
        configuration for configurations in expected_rejections.values() for configuration in configurations
    )
    unvalidated_configs = config_files - validated_configs
    if unvalidated_configs:
        print("FAIL syntax configuration files are not selected by any entry point:")
        for path in sorted(unvalidated_configs):
            print(f"  {path.relative_to(repo_root)}")
        return 1

    additional = [path for path in entry_points if not is_existing_test_file(path, repo_root)]
    failures: list[Path] = []
    report = {}
    with tempfile.TemporaryDirectory(prefix="dynlex-entrypoints-") as temporary_directory:
        for index, path in enumerate(additional, start=1):
            result = check_entry_point(compiler, path, repo_root, Path(temporary_directory) / str(index), args.timeout)
            relative = str(path.relative_to(repo_root))
            report[relative] = {
                "succeeded": result.succeeded,
                "dependencies": sorted(str(source.relative_to(repo_root)) for source in dependencies[path].sources),
                "attempts": [asdict(attempt) for attempt in result.attempts],
            }
            if args.report:
                args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            if result.succeeded:
                print(f"PASS [{index}/{len(additional)}] {relative} ({', '.join(result.accepted_modes)})", flush=True)
            else:
                failures.append(path)
                print(f"FAIL [{index}/{len(additional)}] {relative}", flush=True)
                for attempt in result.attempts:
                    if not attempt.succeeded:
                        print(f"  {attempt.mode}: {first_failure_line(attempt.diagnostics)}", flush=True)

    print(
        f"Entry-point results: {len(additional) - len(failures)} passed, {len(failures)} failed, "
        f"{len(existing_test_files)} files covered by existing tests; "
        f"{len(source_files)} source files reachable and {len(config_files)} syntax configs selected "
        f"({time.monotonic() - started:.2f}s)"
    )
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
