#!/usr/bin/env python3
import json
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def resolved_calls(root: pathlib.Path, name: str) -> tuple[str, list]:
    document = root / "tests" / "lsp" / "call_expressions" / name
    uri = to_file_uri(document)
    source = document.read_text()
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": 1,
                    "text": source,
                }
            },
        )
        calls = session.request("dynlex/callExpressions", {"uri": uri})
        repeated = session.request("dynlex/callExpressions", {"uri": uri})
        assert calls == repeated, "Call-expression responses must be stable"
        errors = [
            diagnostic
            for notification in session.server_notifications
            if notification.get("method") == "textDocument/publishDiagnostics"
            for diagnostic in notification["params"]["diagnostics"]
            if diagnostic.get("severity") == 1
        ]
        assert errors == [], errors
    finally:
        session.close()
    return source, calls


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    _, unused_calls = resolved_calls(root, "unused.dl")
    assert unused_calls == [], "Unused generic templates have no inferred calls"

    source, calls = resolved_calls(root, "instantiated.dl")
    assert len({json.dumps(call, sort_keys=True) for call in calls}) == len(calls), calls
    lines = source.splitlines()
    actual = []
    for call in calls:
        start, end = call["range"]["start"], call["range"]["end"]
        assert start["line"] == end["line"], call
        actual.append((
            start["line"], lines[start["line"]][start["character"]:end["character"]],
            call["definition"]["range"]["start"]["line"], call["returnType"],
        ))
    integer = "a 32-bit integer"
    floating = "a 64-bit floating-point number"
    # Intrinsic argument ranges retain the authored whitespace after the comma.
    expected = [
        (2, "f x", 0, integer), (2, "f x", 1, floating),
        (5, " f x", 0, integer), (5, " f x", 1, floating),
        (8, " g 8", 2, integer), (9, " g 9", 2, integer),
        (10, " g 8.5", 2, floating), (11, " h 8", 3, integer),
        (12, " h 8.5", 3, floating),
        (16, " f x", 0, integer), (17, "f x", 0, integer),
        (19, " k 8", 14, integer),
    ]
    assert actual == expected, f"Expected inferred calls {expected!r}, received {actual!r}"

    source, calls = resolved_calls(root, "main.dl")

    lines = source.splitlines()
    river_commands = []
    for call in calls:
        if call["returnType"] != "nothing":
            continue
        if not call["definition"]["uri"].endswith("/lib/river_challenge.dl"):
            continue
        start = call["range"]["start"]
        end = call["range"]["end"]
        if start["line"] != end["line"]:
            raise RuntimeError(f"river command unexpectedly spans source lines: {call!r}")
        river_commands.append(
            lines[start["line"]][start["character"] : end["character"]]
        )

    expected = [
        "get the sheep in the boat",
        "row to the other side",
    ]
    if river_commands != expected:
        raise RuntimeError(
            f"expected exact call-expression ranges {expected!r}, received {river_commands!r}"
        )

    print("Resolved calls cover inferred overloads and exact ranges, excluding unused and unreachable templates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
