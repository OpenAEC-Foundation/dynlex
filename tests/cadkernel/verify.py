#!/usr/bin/env python3
"""Run existing cadkernel required fixtures without creating expectations."""
from __future__ import annotations

import argparse
import difflib
import fnmatch
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))
from diagnostic_expectations import normalize_diagnostics
from process_error_mode import unattended_child_processes


class FixtureFailure(Exception):
    """A fixture failed to establish its expected behavior."""


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def normalize_output(value: str) -> str:
    # Match scripts/test.sh: CR characters and terminal newlines are ignored.
    return value.replace("\r", "").rstrip("\n")


def compare(label: str, expected: str, actual: str) -> None:
    if actual != expected:
        difference = "\n".join(difflib.unified_diff(
            expected.splitlines(), actual.splitlines(),
            fromfile="expected", tofile="actual", lineterm="",
        ))
        raise FixtureFailure(f"{label} mismatch\n{difference}")


def exit_status(returncode: int) -> int:
    if os.name == "nt":
        status = returncode & 0xFFFFFFFF
        # Same Windows abort mapping as scripts/test.sh.
        return 134 if status == 0xC0000409 else status
    return 128 - returncode if returncode < 0 else returncode


def stop_process_tree(process: subprocess.Popen) -> None:
    if os.name == "nt":
        try:
            stopped = subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NO_WINDOW,
                timeout=5, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            if process.poll() is None:
                process.kill()
            raise FixtureFailure(f"process-tree cleanup failed: {error}") from error
        if stopped.returncode and process.poll() is None:
            process.kill()
            raise FixtureFailure(
                "process-tree cleanup failed: " + stopped.stderr.decode("utf-8", errors="replace")
            )
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_process(
    command: list[str], *, timeout: float, cwd: Path,
    standard_input: bytes = b"", environment: dict[str, str] | None = None,
    stack_kb: int | None = None, phase: str = "process",
) -> tuple[int, str, float]:
    options: dict = {}
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        options["start_new_session"] = True
        if stack_kb is not None:
            def configure_stack() -> None:
                import resource
                _, hard = resource.getrlimit(resource.RLIMIT_STACK)
                resource.setrlimit(resource.RLIMIT_STACK, (stack_kb * 1024, hard))
            options["preexec_fn"] = configure_stack
    started = time.monotonic()
    with unattended_child_processes(), subprocess.Popen(
        command, cwd=cwd, env=environment, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **options,
    ) as process:
        try:
            output, _ = process.communicate(input=standard_input, timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
            cleanup_error = None
            try:
                stop_process_tree(process)
            except FixtureFailure as cleanup:
                cleanup_error = str(cleanup)
            try:
                output, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired as cleanup:
                if process.poll() is None:
                    process.kill()
                process.stdout.close()
                process.wait(timeout=5)
                output = cleanup.output or b""
                cleanup_error = "process tree did not release its output pipe"
            if isinstance(error, KeyboardInterrupt):
                raise
            detail = output.decode("utf-8", errors="replace")
            if cleanup_error:
                detail += "\n" + cleanup_error
            raise FixtureFailure(f"{phase} timed out after {timeout:g}s\n{detail}".rstrip()) from error
        try:
            decoded = output.decode("utf-8")
        except UnicodeDecodeError as error:
            raise FixtureFailure(f"process emitted invalid UTF-8: {error}") from error
        return exit_status(process.returncode), decoded, time.monotonic() - started


def runtime_metadata(fixture: Path) -> tuple[list[str], bytes, Path, dict[str, str]]:
    arguments = []
    arguments_file = fixture / "arguments.txt"
    if arguments_file.is_file():
        content = arguments_file.read_bytes().decode("utf-8")
        arguments = content.split("\n") if content else []
        if arguments and arguments[-1] == "":
            arguments.pop()
        arguments = [argument.removesuffix("\r") for argument in arguments]
        if any("\0" in argument for argument in arguments):
            raise FixtureFailure("NUL byte in arguments.txt")

    stdin_file = fixture / "standard_input.txt"
    bytes_file = fixture / "standard_input.bytes"
    if stdin_file.is_file() and bytes_file.is_file():
        raise FixtureFailure("both standard_input.txt and standard_input.bytes are present")
    standard_input = stdin_file.read_bytes() if stdin_file.is_file() else b""
    if bytes_file.is_file():
        data = bytearray()
        for token in text(bytes_file).split():
            match = re.fullmatch(r"([0-9a-fA-F]{2})(?:\*([1-9][0-9]*))?", token)
            if not match:
                raise FixtureFailure(f"invalid standard_input.bytes token: {token}")
            data.extend(bytes([int(match[1], 16)]) * int(match[2] or 1))
        standard_input = bytes(data)

    cwd = ROOT
    cwd_file = fixture / "working_directory.txt"
    if cwd_file.is_file():
        value = text(cwd_file).rstrip("\n")
        if not value or any(character in value for character in "\r\n\0"):
            raise FixtureFailure("working_directory.txt must contain one nonempty path")
        cwd = (ROOT / value).resolve()
        if not cwd.is_dir():
            raise FixtureFailure(f"runtime working directory does not exist: {cwd}")

    environment = os.environ.copy()
    environment_file = fixture / "environment.txt"
    if environment_file.is_file():
        names = set()
        for line in text(environment_file).splitlines():
            name, separator, value = line.partition("=")
            key = name.upper() if os.name == "nt" else name
            if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or "\0" in value:
                raise FixtureFailure(f"invalid environment.txt entry: {line!r}")
            if key in names:
                raise FixtureFailure(f"duplicate environment.txt variable: {name}")
            names.add(key)
            environment[key] = value
    return arguments, standard_input, cwd, environment


def expected_runtime_exit(fixture: Path) -> int:
    path = fixture / "expected_runtime_failure.txt"
    if not path.is_file():
        return 0
    value = text(path).strip()
    if value == "abort":
        return 134 if os.name == "nt" else 128 + signal.SIGABRT
    if re.fullmatch(r"exit:[0-9]+", value):
        status = int(value.removeprefix("exit:"))
        if 1 <= status <= 255:
            return status
    raise FixtureFailure(f"invalid expected_runtime_failure.txt: {value!r}")


def verify_fixture(
    fixture: Path, optimization: str, compiler: Path, output_directory: Path,
    compile_timeout: float, run_timeout: float, verbose: bool,
) -> str:
    source = fixture / "main.dl"
    expected_file = fixture / "expected.txt"
    diagnostics_file = fixture / "expected_diagnostics.txt"
    if not source.is_file():
        raise FixtureFailure("missing main.dl")
    if not expected_file.is_file() and not diagnostics_file.is_file():
        raise FixtureFailure("missing expected.txt or expected_diagnostics.txt")
    expected_diagnostics = normalize_diagnostics(
        text(diagnostics_file), ROOT, reject_line_numbers=True,
    ) if diagnostics_file.is_file() else ""
    compile_failure = diagnostics_file.is_file() and not expected_file.is_file()
    if compile_failure and not expected_diagnostics:
        raise FixtureFailure("expected compile failure requires nonempty diagnostics")
    if compile_failure and (fixture / "expected_runtime_failure.txt").exists():
        raise FixtureFailure("compile-only diagnostics conflict with a runtime failure expectation")
    stack_kb = None
    stack_file = fixture / "stack_limit_kb.txt"
    if stack_file.is_file():
        value = text(stack_file).strip()
        if not re.fullmatch(r"[1-9][0-9]*", value):
            raise FixtureFailure("invalid stack_limit_kb.txt")
        stack_kb = int(value)

    # Unique run directory and per-mode filename prevent execution of stale output.
    binary = output_directory / f"{fixture.name}-{optimization}.out"
    command = [str(compiler), str(source.relative_to(ROOT)), f"-{optimization}", "-o", str(binary)]
    if verbose:
        print("  compile:", subprocess.list2cmdline(command), flush=True)
    status, output, compile_seconds = run_process(
        command, timeout=compile_timeout, cwd=ROOT, stack_kb=stack_kb, phase="compilation",
    )
    if status >= 128:
        raise FixtureFailure(f"compiler terminated abnormally (exit {status})\n{output}".rstrip())
    compare("compiler diagnostics", expected_diagnostics, normalize_diagnostics(output, ROOT))
    if compile_failure:
        if status == 0 or binary.exists():
            raise FixtureFailure("expected rejected compilation without a binary, but compilation succeeded or emitted output")
        return f"expected compile failure; compile={compile_seconds:.3f}s"
    if status != 0:
        raise FixtureFailure(f"compilation failed (exit {status})\n{output}".rstrip())
    if not binary.is_file():
        raise FixtureFailure("compiler exited successfully without the requested .out binary")

    expected_output = normalize_output(text(expected_file))
    expected_exit = expected_runtime_exit(fixture)
    arguments, standard_input, cwd, environment = runtime_metadata(fixture)
    command = [str(binary), *arguments]
    if verbose:
        print("  run:", subprocess.list2cmdline(command), flush=True)
    status, output, run_seconds = run_process(
        command, timeout=run_timeout, cwd=cwd,
        standard_input=standard_input, environment=environment, phase="execution",
    )
    if status != expected_exit:
        raise FixtureFailure(f"runtime exit {status}; expected {expected_exit}\n{output}".rstrip())
    compare("runtime output", expected_output, normalize_output(output))
    result = "expected runtime failure" if expected_exit else "output matched"
    return f"{result}; compile={compile_seconds:.3f}s run={run_seconds:.3f}s"


def positive_seconds(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return number


def fixture_compile_timeout(fixture: Path, override: float | None) -> float:
    option = fixture / "compile_timeout_seconds.txt"
    declared = 30.0
    if option.is_file():
        value = text(option).strip()
        if not re.fullmatch(r"[1-9][0-9]{0,2}", value) or int(value) > 300:
            raise FixtureFailure("invalid compile_timeout_seconds.txt")
        declared = float(value)
    return override if override is not None else declared


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build/dynlex.exe" if os.name == "nt" else "build/dynlex"))
    parser.add_argument("--filter", action="append", dest="filters", metavar="GLOB", help="fixture-name glob; repeat to select the union")
    parser.add_argument("--optimization", action="append", choices=("O0", "O2"), help="repeatable; defaults to both O0 and O2")
    parser.add_argument("--compile-timeout", type=positive_seconds, metavar="SECONDS", help="override each fixture's compilation budget")
    parser.add_argument("--run-timeout", type=positive_seconds, default=15.0, metavar="SECONDS")
    parser.add_argument("--list", action="store_true", help="list selected fixture names without compiling")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    candidates = sorted(path for path in (ROOT / "tests/required").glob("cadkernel_*") if path.is_dir())
    filters = args.filters or ["*"]
    for pattern in filters:
        if not any(fnmatch.fnmatchcase(path.name, pattern) for path in candidates):
            parser.error(f"no cadkernel fixture matches {pattern!r}")
    fixtures = [path for path in candidates if any(fnmatch.fnmatchcase(path.name, pattern) for pattern in filters)]
    if args.list:
        print("\n".join(path.name for path in fixtures))
        return 0
    compiler = (ROOT / args.compiler).resolve()
    if not compiler.is_file():
        parser.error(f"compiler not found: {compiler}")
    optimizations = list(dict.fromkeys(args.optimization or ["O0", "O2"]))
    build_directory = ROOT / "build/cadkernel-verify"
    build_directory.mkdir(parents=True, exist_ok=True)
    output_directory = Path(tempfile.mkdtemp(prefix="run-", dir=build_directory))
    print(f"Compiler: {compiler}\nArtifacts: {output_directory}", flush=True)
    print(f"Selected {len(fixtures)} fixtures at {', '.join(optimizations)}; full main-crate scope remains 83 source files / 672 upstream tests.", flush=True)
    passed = failed = 0
    started = time.monotonic()
    for fixture in fixtures:
        for optimization in optimizations:
            label = f"{fixture.name} -{optimization}"
            print(f"RUN  {label}", flush=True)
            try:
                detail = verify_fixture(
                    fixture, optimization, compiler, output_directory,
                    fixture_compile_timeout(fixture, args.compile_timeout), args.run_timeout, args.verbose,
                )
            except (FixtureFailure, OSError, ValueError, subprocess.SubprocessError) as error:
                failed += 1
                print(f"FAIL {label}: {error}", flush=True)
            else:
                passed += 1
                print(f"PASS {label}: {detail}", flush=True)
            if failed and args.fail_fast:
                break
        if failed and args.fail_fast:
            break
    unrun = len(fixtures) * len(optimizations) - passed - failed
    print(f"Fixture-mode results: {passed} passed, {failed} failed, {unrun} not run ({time.monotonic() - started:.3f}s).", flush=True)
    print("These results cover the selected fixtures only; they do not establish full-port completion.", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Interrupted; verification incomplete.", file=sys.stderr)
        raise SystemExit(130)
