#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests" / "lsp" / "call_expressions" / "main.dl"
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
    finally:
        session.close()

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

    print("Resolved call expressions retain exact ranges for commands joined with 'and'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
