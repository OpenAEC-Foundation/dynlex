#!/usr/bin/env python3
"""
Headless Natvis probe for C++ debugging (OpenDebugAD7 + DAP).

This script launches the cppdbg adapter, stops at a source breakpoint, and
prints Natvis-rendered values for selected locals so Natvis changes can be
iterated without opening VS Code UI.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


def encode_dap(payload: dict[str, Any]) -> bytes:
    body = json.dumps(payload, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}".encode("utf-8")


class DapClient:
    def __init__(self, process: subprocess.Popen[bytes]) -> None:
        self.process = process
        self.seq = 1

    def send(self, command: str, arguments: dict[str, Any] | None = None) -> int:
        request: dict[str, Any] = {"seq": self.seq, "type": "request", "command": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.seq += 1
        assert self.process.stdin is not None
        self.process.stdin.write(encode_dap(request))
        self.process.stdin.flush()
        return request["seq"]

    def read_message(self, timeout_seconds: float = 30.0) -> dict[str, Any]:
        assert self.process.stdout is not None
        deadline = time.time() + timeout_seconds
        header = b""
        while b"\r\n\r\n" not in header:
            if time.time() > deadline:
                raise TimeoutError("timed out waiting for DAP header")
            byte = self.process.stdout.read(1)
            if not byte:
                raise RuntimeError("debug adapter closed while reading header")
            header += byte

        header_blob, body_start = header.split(b"\r\n\r\n", 1)
        content_length = None
        for line in header_blob.decode("utf-8", errors="replace").split("\r\n"):
            if line.lower().startswith("content-length:"):
                content_length = int(line.split(":", 1)[1].strip())
                break
        if content_length is None:
            raise RuntimeError("DAP message missing Content-Length")

        body = body_start
        while len(body) < content_length:
            chunk = self.process.stdout.read(content_length - len(body))
            if not chunk:
                raise RuntimeError("debug adapter closed while reading body")
            body += chunk
        return json.loads(body[:content_length].decode("utf-8", errors="replace"))

    def wait_response(self, request_seq: int, timeout_seconds: float = 60.0) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        deadline = time.time() + timeout_seconds
        backlog: list[dict[str, Any]] = []
        while time.time() < deadline:
            message = self.read_message(timeout_seconds=max(0.5, deadline - time.time()))
            if message.get("type") == "response" and message.get("request_seq") == request_seq:
                return message, backlog
            backlog.append(message)
        raise TimeoutError(f"timed out waiting for response to request #{request_seq}")

    def wait_event(self, name: str, timeout_seconds: float = 120.0) -> dict[str, Any]:
        deadline = time.time() + timeout_seconds
        while time.time() < deadline:
            message = self.read_message(timeout_seconds=max(0.5, deadline - time.time()))
            if message.get("type") == "event" and message.get("event") == name:
                return message
        raise TimeoutError(f"timed out waiting for event '{name}'")


def find_opendebugad7(explicit_path: str | None) -> Path:
    if explicit_path:
        path = Path(explicit_path).expanduser()
        if not path.is_file():
            raise FileNotFoundError(f"OpenDebugAD7 not found: {path}")
        return path

    candidates = sorted(Path.home().glob(".vscode/extensions/ms-vscode.cpptools-*-linux-x64/debugAdapters/bin/OpenDebugAD7"))
    if candidates:
        return candidates[-1]
    raise FileNotFoundError("OpenDebugAD7 not found. Install the VS Code C/C++ extension or pass --adapter-path.")


def normalize_path(path: str, root: Path) -> str:
    p = Path(path)
    if not p.is_absolute():
        p = root / p
    return str(p.resolve())


def request_ok_or_raise(response: dict[str, Any], operation: str) -> dict[str, Any]:
    if response.get("success"):
        return response
    message = response.get("message", "<no message>")
    raise RuntimeError(f"{operation} failed: {message}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Headless Natvis probe using OpenDebugAD7.")
    parser.add_argument("--workspace", default=".", help="DynLex repository root (default: current directory).")
    parser.add_argument("--adapter-path", help="Explicit OpenDebugAD7 path.")
    parser.add_argument("--program", default="build/dynlex", help="Program path to launch.")
    parser.add_argument("--dl-file", default="tests/required/simple/main.dl", help="DynLex input file argument.")
    parser.add_argument("--extra-arg", action="append", default=[], help="Additional program arg (repeatable).")
    parser.add_argument("--source", default="src/cpp/compiler/parseContext.cpp", help="Breakpoint source file.")
    parser.add_argument("--line", type=int, default=95, help="Breakpoint line (1-based).")
    parser.add_argument("--hit-condition", help="Optional DAP breakpoint hitCondition (for example: 20).")
    parser.add_argument("--natvis", default=".vscode/dynlex.natvis", help="Natvis file path.")
    parser.add_argument(
        "--vars", default="currentProgress,reference,queue", help="Comma-separated local variable names to print."
    )
    parser.add_argument("--expand", default="currentProgress", help="Variable name to expand once.")
    parser.add_argument("--timeout", type=float, default=120.0, help="Timeout seconds for breakpoint wait.")
    parser.add_argument("--natvis-diagnostics", action="store_true", help="Enable OpenDebugAD7 natvis diagnostics.")
    args = parser.parse_args()

    workspace = Path(args.workspace).expanduser().resolve()
    adapter = find_opendebugad7(args.adapter_path)
    program = normalize_path(args.program, workspace)
    dl_file = normalize_path(args.dl_file, workspace)
    source = normalize_path(args.source, workspace)
    natvis = normalize_path(args.natvis, workspace)

    adapter_cmd = [str(adapter)]
    if args.natvis_diagnostics:
        adapter_cmd.append("--natvisDiagnostics")

    process = subprocess.Popen(adapter_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    dap = DapClient(process)

    try:
        init_seq = dap.send(
            "initialize",
            {
                "adapterID": "cppdbg",
                "linesStartAt1": True,
                "columnsStartAt1": True,
                "pathFormat": "path",
                "supportsVariableType": True,
                "supportsVariablePaging": True,
                "supportsRunInTerminalRequest": False,
                "supportsInvalidatedEvent": True,
            },
        )
        request_ok_or_raise(dap.wait_response(init_seq)[0], "initialize")

        launch_seq = dap.send(
            "launch",
            {
                "name": "Natvis Probe",
                "type": "cppdbg",
                "request": "launch",
                "program": program,
                "args": [dl_file, *args.extra_arg],
                "cwd": str(workspace),
                "externalConsole": False,
                "MIMode": "gdb",
                "miDebuggerPath": "/usr/bin/gdb",
                "stopAtEntry": False,
                "visualizerFile": natvis,
                "showDisplayString": True,
            },
        )
        request_ok_or_raise(dap.wait_response(launch_seq, timeout_seconds=120.0)[0], "launch")

        breakpoint: dict[str, Any] = {"line": args.line}
        if args.hit_condition:
            breakpoint["hitCondition"] = args.hit_condition
        bp_seq = dap.send("setBreakpoints", {"source": {"path": source}, "breakpoints": [breakpoint], "sourceModified": False})
        bp_response = request_ok_or_raise(dap.wait_response(bp_seq)[0], "setBreakpoints")
        print("breakpoints:", bp_response.get("body", {}).get("breakpoints", []))

        ex_seq = dap.send("setExceptionBreakpoints", {"filters": []})
        request_ok_or_raise(dap.wait_response(ex_seq)[0], "setExceptionBreakpoints")

        done_seq = dap.send("configurationDone", {})
        request_ok_or_raise(dap.wait_response(done_seq)[0], "configurationDone")

        stopped = dap.wait_event("stopped", timeout_seconds=args.timeout)
        print("stopped:", stopped.get("body", {}))
        thread_id = stopped.get("body", {}).get("threadId")
        if not thread_id:
            raise RuntimeError("stopped event did not include threadId")

        threads_seq = dap.send("threads", {})
        request_ok_or_raise(dap.wait_response(threads_seq)[0], "threads")

        stack_seq = dap.send("stackTrace", {"threadId": thread_id, "startFrame": 0, "levels": 5})
        stack_response = request_ok_or_raise(dap.wait_response(stack_seq)[0], "stackTrace")
        frames = stack_response.get("body", {}).get("stackFrames", [])
        if not frames:
            raise RuntimeError("stackTrace returned no frames")
        top = frames[0]
        print("top-frame:", top.get("name"), "line", top.get("line"))

        scopes_seq = dap.send("scopes", {"frameId": top["id"]})
        scopes_response = request_ok_or_raise(dap.wait_response(scopes_seq)[0], "scopes")
        scopes = scopes_response.get("body", {}).get("scopes", [])
        if not scopes:
            raise RuntimeError("scopes returned no scopes")
        locals_scope = next((scope for scope in scopes if scope["name"].lower().startswith("local")), scopes[0])

        vars_seq = dap.send("variables", {"variablesReference": locals_scope["variablesReference"]})
        vars_response = request_ok_or_raise(dap.wait_response(vars_seq)[0], "variables(locals)")
        local_vars = vars_response.get("body", {}).get("variables", [])

        wanted = {name.strip() for name in args.vars.split(",") if name.strip()}
        picked = [var for var in local_vars if var.get("name") in wanted]

        print("locals:")
        for var in picked:
            print(f"  {var.get('name')}: {var.get('value')}")

        expand_target = next((var for var in picked if var.get("name") == args.expand and var.get("variablesReference", 0) > 0), None)
        if expand_target:
            expand_seq = dap.send("variables", {"variablesReference": expand_target["variablesReference"]})
            expand_response = request_ok_or_raise(dap.wait_response(expand_seq)[0], f"variables({args.expand})")
            children = expand_response.get("body", {}).get("variables", [])
            print(f"{args.expand} children:")
            for child in children:
                print(f"  {child.get('name')}: {child.get('value')}")

        disconnect_seq = dap.send("disconnect", {"terminateDebuggee": True})
        request_ok_or_raise(dap.wait_response(disconnect_seq, timeout_seconds=20.0)[0], "disconnect")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()

        assert process.stderr is not None
        stderr_text = process.stderr.read().decode("utf-8", errors="replace").strip()
        if stderr_text:
            print("adapter-stderr:")
            for line in stderr_text.splitlines()[-20:]:
                print(f"  {line}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # pragma: no cover - script entrypoint
        print(f"natvis_probe error: {error}", file=sys.stderr)
        raise SystemExit(1)
