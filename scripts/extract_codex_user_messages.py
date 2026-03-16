#!/usr/bin/env python3
"""Extract user messages for Codex sessions tied to a specific repo cwd.

The script streams session files from ~/.codex/sessions, keeps only user
messages, and skips oversized or dump-like payloads by default so agents can
analyze prompt history without blowing up context.
"""

from __future__ import annotations

import argparse
import json
import signal
import re
import sys
from pathlib import Path
from typing import Iterator


DEFAULT_CWD = "/home/johnheikens/Documents/Github/DynLex"
DEFAULT_SESSIONS_ROOT = Path("/home/johnheikens/.codex/sessions")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cwd", default=DEFAULT_CWD, help="Session cwd to match")
    parser.add_argument(
        "--sessions-root",
        type=Path,
        default=DEFAULT_SESSIONS_ROOT,
        help="Root containing Codex session JSONL files",
    )
    parser.add_argument(
        "--max-chars",
        type=int,
        default=12000,
        help="Skip messages longer than this many characters",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=200,
        help="Maximum number of messages to emit after filtering",
    )
    parser.add_argument(
        "--preview-chars",
        type=int,
        default=220,
        help="Preview length in text mode",
    )
    parser.add_argument(
        "--format",
        choices=("text", "jsonl"),
        default="text",
        help="Output format",
    )
    parser.add_argument(
        "--include-skipped",
        action="store_true",
        help="Emit skipped messages with a skip reason instead of dropping them",
    )
    parser.add_argument(
        "--reverse",
        action="store_true",
        help="Read newest session files first",
    )
    return parser.parse_args()


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def looks_like_dump(text: str) -> str | None:
    lower = text.lower()

    if "<html" in lower or "<body" in lower or "<div" in lower:
        return "html_dump"
    if "outerhtml" in lower or "innerhtml" in lower or "document.queryselector" in lower:
        return "dom_dump"
    if text.count("{") + text.count("}") > 400:
        return "json_like_dump"
    if text.count("<") + text.count(">") > 400:
        return "markup_like_dump"
    if text.count("\n") > 1200:
        return "large_multiline_dump"

    return None


def iter_session_paths(root: Path) -> Iterator[Path]:
    yield from sorted(root.rglob("*.jsonl"))


def session_matches(path: Path, cwd: str) -> tuple[bool, dict]:
    try:
        with path.open() as handle:
            first_line = handle.readline()
    except OSError:
        return False, {}

    if not first_line:
        return False, {}

    try:
        first = json.loads(first_line)
    except json.JSONDecodeError:
        return False, {}

    if first.get("type") != "session_meta":
        return False, {}

    payload = first.get("payload", {})
    return payload.get("cwd") == cwd, payload


def extract_messages(path: Path, session_meta: dict) -> Iterator[dict]:
    with path.open() as handle:
        for line in handle:
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue

            if obj.get("type") != "event_msg":
                continue

            payload = obj.get("payload", {})
            if payload.get("type") != "user_message":
                continue

            message = payload.get("message", "")
            yield {
                "session_id": session_meta.get("id"),
                "thread_name": session_meta.get("thread_name", ""),
                "session_timestamp": session_meta.get("timestamp"),
                "message_timestamp": obj.get("timestamp"),
                "path": str(path),
                "length": len(message),
                "message": message,
            }


def main() -> int:
    args = parse_args()
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)

    emitted = 0
    paths = list(iter_session_paths(args.sessions_root))
    if args.reverse:
        paths.reverse()

    for path in paths:
        matches, session_meta = session_matches(path, args.cwd)
        if not matches:
            continue

        for item in extract_messages(path, session_meta):
            reason = None
            if item["length"] > args.max_chars:
                reason = "too_large"
            else:
                reason = looks_like_dump(item["message"])

            if reason and not args.include_skipped:
                continue

            record = {
                "session_id": item["session_id"],
                "thread_name": item["thread_name"],
                "message_timestamp": item["message_timestamp"],
                "path": item["path"],
                "length": item["length"],
                "skip_reason": reason,
                "message": item["message"],
            }

            if args.format == "jsonl":
                try:
                    print(json.dumps(record, ensure_ascii=False))
                except BrokenPipeError:
                    return 0
            else:
                preview = normalize_whitespace(item["message"])[: args.preview_chars]
                status = reason or "ok"
                try:
                    print(
                        f"[{status}] {item['message_timestamp']} {item['thread_name']} "
                        f"len={item['length']} :: {preview}"
                    )
                except BrokenPipeError:
                    return 0

            emitted += 1
            if emitted >= args.limit:
                return 0

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:
        sys.exit(0)
