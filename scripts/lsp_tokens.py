#!/usr/bin/env python3
import argparse
import json
import os
import pathlib
import subprocess
import sys
import threading
from typing import Any


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent.parent


def default_server_path(root: pathlib.Path) -> pathlib.Path:
    override = os.environ.get("DYNLEX_LSP_SERVER")
    if override:
        override_path = pathlib.Path(override)
        return override_path if override_path.is_absolute() else (root / override_path).resolve()
    compiler = root / "build" / "dynlex"
    windows_compiler = compiler.with_suffix(".exe")
    if not compiler.is_file() and windows_compiler.is_file():
        return windows_compiler
    return compiler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send LSP requests to DynLex over stdio and print semantic token responses."
    )
    parser.add_argument("file", nargs="?", help="Document path for simple mode.")
    parser.add_argument(
        "--scenario",
        help="JSON file describing an LSP session. If set, simple-mode flags are ignored.",
    )
    parser.add_argument(
        "--server",
        default="./build/dynlex",
        help="Path to the DynLex executable. Default: ./build/dynlex",
    )
    parser.add_argument(
        "--cursor",
        help="Active cursor in simple mode as LINE:CHAR (0-based). Sends dynlex/activeCursorChanged.",
    )
    parser.add_argument(
        "--client-id",
        default="lsp-token-script",
        help="Client id for dynlex/activeCursorChanged.",
    )
    parser.add_argument(
        "--change-json",
        action="append",
        default=[],
        help="Raw JSON TextDocumentContentChangeEvent to apply in simple mode. Repeatable.",
    )
    parser.add_argument(
        "--tokens-after-open",
        action="store_true",
        help="Request semantic tokens immediately after didOpen in simple mode.",
    )
    parser.add_argument(
        "--tokens-after-each-change",
        action="store_true",
        help="Request semantic tokens after every didChange in simple mode.",
    )
    parser.add_argument(
        "--print-messages",
        action="store_true",
        help="Print every JSON-RPC payload exchanged with the server.",
    )
    parser.add_argument(
        "--print-stderr",
        action="store_true",
        help="Mirror server stderr while running.",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Request raw semantic token integer arrays instead of tagged source output.",
    )
    return parser.parse_args()


def to_file_uri(path: pathlib.Path) -> str:
    return path.resolve().as_uri()


class LspSession:
    def __init__(self, server_path: pathlib.Path, cwd: pathlib.Path, print_messages: bool, print_stderr: bool) -> None:
        self.server_path = server_path
        self.cwd = cwd
        self.print_messages = print_messages
        self.print_stderr = print_stderr
        self.request_id = 0
        self.server_requests: list[dict[str, Any]] = []
        self.server_notifications: list[dict[str, Any]] = []
        self.process = subprocess.Popen(
            [str(server_path), "--stdio"],
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        if self.process.stdin is None or self.process.stdout is None or self.process.stderr is None:
            raise RuntimeError("failed to start stdio transport")

        self._stderr_lines: list[str] = []
        self._stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._stderr_thread.start()

    def _drain_stderr(self) -> None:
        assert self.process.stderr is not None
        while True:
            chunk = self.process.stderr.readline()
            if not chunk:
                return
            line = chunk.decode("utf-8", errors="replace")
            self._stderr_lines.append(line)
            if self.print_stderr:
                sys.stderr.write(line)

    def close(self) -> None:
        try:
            self.request("shutdown")
        except Exception:
            pass
        try:
            self.notify("exit")
        except Exception:
            pass
        try:
            shutdown_timeout = float(os.environ.get("DYNLEX_LSP_SHUTDOWN_TIMEOUT", "10"))
            self.process.wait(timeout=shutdown_timeout)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=2)
        self._stderr_thread.join(timeout=1)
        if self.process.returncode != 0:
            stderr = "".join(self._stderr_lines).strip()
            raise RuntimeError(
                f"language server exited with status {self.process.returncode}"
                + (f": {stderr}" if stderr else "")
            )

    def _write_message(self, payload: dict[str, Any]) -> None:
        assert self.process.stdin is not None
        body = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
        if self.print_messages:
            print(">>>", json.dumps(payload, indent=2))
        self.process.stdin.write(header + body)
        self.process.stdin.flush()

    def _read_message(self) -> dict[str, Any]:
        assert self.process.stdout is not None
        headers = b""
        while b"\r\n\r\n" not in headers:
            chunk = self.process.stdout.read(1)
            if not chunk:
                raise RuntimeError("language server closed stdout unexpectedly")
            headers += chunk
        header_text = headers.decode("ascii", errors="replace")
        content_length = None
        for line in header_text.split("\r\n"):
            if line.lower().startswith("content-length:"):
                content_length = int(line.split(":", 1)[1].strip())
                break
        if content_length is None:
            raise RuntimeError(f"missing Content-Length header: {header_text!r}")
        body = self.process.stdout.read(content_length)
        if len(body) != content_length:
            raise RuntimeError("unexpected EOF while reading language server response body")
        message = json.loads(body.decode("utf-8"))
        if self.print_messages:
            print("<<<", json.dumps(message, indent=2))
        return message

    def request(self, method: str, params: dict[str, Any] | None = None) -> Any:
        self.request_id += 1
        request_id = self.request_id
        payload = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            payload["params"] = params
        self._write_message(payload)
        while True:
            message = self._read_message()
            if "method" in message:
                if "id" in message:
                    self.server_requests.append(message)
                    if message["method"] != "workspace/semanticTokens/refresh":
                        raise RuntimeError(f"unsupported language-server request: {message['method']}")
                    self._write_message({"jsonrpc": "2.0", "id": message["id"], "result": None})
                else:
                    self.server_notifications.append(message)
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise RuntimeError(f"{method} failed: {json.dumps(message['error'])}")
            return message.get("result")

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        self._write_message(payload)


def initialize_session(session: LspSession, root: pathlib.Path) -> None:
    session.request(
        "initialize",
        {
            "processId": None,
            "rootUri": to_file_uri(root),
            "capabilities": {},
        },
    )
    session.notify("initialized", {})


def apply_change_to_text(text: str, change: dict[str, Any]) -> str:
    range_value = change.get("range")
    if range_value is None:
        return str(change["text"])

    def position_to_offset(source: str, position: dict[str, int]) -> int:
        line = max(0, int(position["line"]))
        character = max(0, int(position["character"]))
        current_line = 0
        offset = 0
        while current_line < line and offset < len(source):
            if source[offset] == "\r":
                offset += 1
                if offset < len(source) and source[offset] == "\n":
                    offset += 1
                current_line += 1
                continue
            if source[offset] == "\n":
                offset += 1
                current_line += 1
                continue
            offset += 1

        line_end = offset
        while line_end < len(source) and source[line_end] not in "\r\n":
            line_end += 1
        return min(offset + character, line_end)

    start = position_to_offset(text, range_value["start"])
    end = position_to_offset(text, range_value["end"])
    return text[:start] + str(change["text"]) + text[end:]


def print_tokens(label: str, result: Any, raw: bool) -> None:
    print(f"== {label} ==")
    if raw:
        print(json.dumps(result.get("data", []), indent=2))
    else:
        print(str(result).rstrip())


def run_simple_mode(args: argparse.Namespace) -> int:
    if not args.file:
        raise SystemExit("simple mode requires a file path")

    root = repo_root()
    server_path = (
        default_server_path(root)
        if args.server == "./build/dynlex"
        else (root / args.server).resolve() if not pathlib.Path(args.server).is_absolute() else pathlib.Path(args.server)
    )
    file_path = (root / args.file).resolve() if not pathlib.Path(args.file).is_absolute() else pathlib.Path(args.file)
    uri = to_file_uri(file_path)
    text = file_path.read_text()
    version = 1

    session = LspSession(server_path, root, args.print_messages, args.print_stderr)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": version,
                    "text": text,
                }
            },
        )

        if args.cursor:
            line_text, char_text = args.cursor.split(":", 1)
            session.notify(
                "dynlex/activeCursorChanged",
                {
                    "clientId": args.client_id,
                    "uri": uri,
                    "version": version,
                    "position": {"line": int(line_text), "character": int(char_text)},
                },
            )

        if args.tokens_after_open:
            method = "textDocument/semanticTokens/full" if args.raw else "dynlex/renderSemanticTokens"
            result = session.request(method, {"uri": uri} if not args.raw else {"textDocument": {"uri": uri}})
            print_tokens("after open", result, args.raw)

        for index, raw_change in enumerate(args.change_json, 1):
            change = json.loads(raw_change)
            version += 1
            text = apply_change_to_text(text, change)
            session.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": version},
                    "contentChanges": [change],
                },
            )
            if args.cursor:
                line_text, char_text = args.cursor.split(":", 1)
                session.notify(
                    "dynlex/activeCursorChanged",
                    {
                        "clientId": args.client_id,
                        "uri": uri,
                        "version": version,
                        "position": {"line": int(line_text), "character": int(char_text)},
                    },
                )
            if args.tokens_after_each_change:
                method = "textDocument/semanticTokens/full" if args.raw else "dynlex/renderSemanticTokens"
                result = session.request(method, {"uri": uri} if not args.raw else {"textDocument": {"uri": uri}})
                print_tokens(f"after change {index}", result, args.raw)
    finally:
        session.close()
    return 0


def run_scenario(args: argparse.Namespace) -> int:
    root = repo_root()
    server_path = (
        default_server_path(root)
        if args.server == "./build/dynlex"
        else (root / args.server).resolve() if not pathlib.Path(args.server).is_absolute() else pathlib.Path(args.server)
    )
    scenario_path = (root / args.scenario).resolve() if not pathlib.Path(args.scenario).is_absolute() else pathlib.Path(args.scenario)
    scenario = json.loads(scenario_path.read_text())
    session = LspSession(server_path, root, args.print_messages, args.print_stderr)
    try:
        initialize_session(session, root)
        for index, step in enumerate(scenario.get("steps", []), 1):
            if "notify" in step:
                method = step["notify"]
                params = step.get("params")
                session.notify(method, params)
                continue
            if "request" in step:
                result = session.request(step["request"], step.get("params"))
                label = step.get("label", f"step {index}")
                if step["request"] in {"textDocument/semanticTokens/full", "dynlex/renderSemanticTokens"}:
                    print_tokens(label, result, step["request"] == "textDocument/semanticTokens/full")
                else:
                    print(f"== {label} ==")
                    print(json.dumps(result, indent=2))
                continue
            raise RuntimeError(f"scenario step {index} must contain 'notify' or 'request'")
    finally:
        session.close()
    return 0


def main() -> int:
    args = parse_args()
    if args.scenario:
        return run_scenario(args)
    return run_simple_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
