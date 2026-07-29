#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


CALL_CHARACTER = 7
FIRST_CALL_LINE = 4
SECOND_CALL_LINE = 5


def cursor_notification(uri: str, version: int, line: int) -> dict:
    return {
        "clientId": "cursor-commit-regression",
        "uri": uri,
        "version": version,
        "position": {"line": line, "character": CALL_CHARACTER + 1},
    }


def replace_number(session: LspSession, uri: str, version: int, replacement: str) -> None:
    session.notify(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": version},
            "contentChanges": [
                {
                    "range": {
                        "start": {
                            "line": FIRST_CALL_LINE,
                            "character": CALL_CHARACTER,
                        },
                        "end": {
                            "line": FIRST_CALL_LINE,
                            "character": CALL_CHARACTER + 1,
                        },
                    },
                    "text": replacement,
                }
            ],
        },
    )


def token_lines(data: list[int]) -> set[int]:
    if len(data) % 5 != 0:
        raise RuntimeError(f"semantic token data is not grouped in fives: {data}")
    line = 0
    lines: set[int] = set()
    for index in range(0, len(data), 5):
        line += data[index]
        lines.add(line)
    return lines


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    server = default_server_path(root)
    document = root / "tests" / "lsp" / "cursor_commit" / "main.dl"
    uri = to_file_uri(document)
    session = LspSession(server, root, False, False)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": 1,
                    "text": document.read_text(),
                }
            },
        )

        session.notify(
            "dynlex/activeCursorChanged",
            cursor_notification(uri, 1, FIRST_CALL_LINE),
        )
        replace_number(session, uri, 2, ")")
        session.notify(
            "dynlex/activeCursorChanged",
            cursor_notification(uri, 2, SECOND_CALL_LINE),
        )
        session.notify(
            "dynlex/activeCursorChanged",
            cursor_notification(uri, 2, FIRST_CALL_LINE),
        )
        replace_number(session, uri, 3, "1")
        session.notify(
            "dynlex/activeCursorChanged",
            cursor_notification(uri, 3, SECOND_CALL_LINE),
        )

        result = session.request(
            "textDocument/semanticTokens/full",
            {"textDocument": {"uri": uri}},
        )

        refreshes = [
            message
            for message in session.server_requests
            if message.get("method") == "workspace/semanticTokens/refresh"
        ]
        if len(refreshes) != 5:
            raise RuntimeError(f"expected 5 semantic-token refreshes, received {len(refreshes)}")

        diagnostics = [
            message["params"]["diagnostics"]
            for message in session.server_notifications
            if message.get("method") == "textDocument/publishDiagnostics"
            and message.get("params", {}).get("uri") == uri
        ]
        if not any(diagnostic_set for diagnostic_set in diagnostics):
            raise RuntimeError("the committed invalid line did not produce diagnostics")
        if not diagnostics or diagnostics[-1]:
            raise RuntimeError("diagnostics were not cleared after committing the repaired line")

        lines = token_lines(result.get("data", []))
        if not {FIRST_CALL_LINE, SECOND_CALL_LINE}.issubset(lines):
            raise RuntimeError(f"semantic highlighting did not recover for both lines: {sorted(lines)}")
    finally:
        session.close()

    print("Cursor line commits refresh semantic highlighting after diagnostics clear")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
