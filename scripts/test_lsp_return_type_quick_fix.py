#!/usr/bin/env python3
import pathlib

from lsp_tokens import (
    LspSession,
    apply_change_to_text,
    default_server_path,
    initialize_session,
    to_file_uri,
)


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    server = default_server_path(root)
    document = root / "tests" / "required" / "incompatible_return_type" / "main.dl"
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
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        published = [
            message["params"]["diagnostics"]
            for message in session.server_notifications
            if message.get("method") == "textDocument/publishDiagnostics"
            and message.get("params", {}).get("uri") == uri
            and message["params"]["diagnostics"]
        ]
        if not published:
            raise RuntimeError("the incompatible return types did not produce an LSP diagnostic")
        diagnostics = published[-1]
        if len(diagnostics) != 1:
            raise RuntimeError(f"expected one diagnostic, received {len(diagnostics)}")

        actions = session.request(
            "textDocument/codeAction",
            {
                "textDocument": {"uri": uri},
                "range": diagnostics[0]["range"],
                "context": {"diagnostics": diagnostics},
            },
        )
        if len(actions) != 1:
            raise RuntimeError(f"expected one return conversion action, received {len(actions)}")
        action = actions[0]
        expected_replacement = '@intrinsic("cast", value, @intrinsic("type", "int", 32))'
        edits = action.get("edit", {}).get("changes", {}).get(uri, [])
        if action.get("kind") != "quickfix" or action.get("title") != "Convert returned value to a 32 bit integer":
            raise RuntimeError(f"unexpected code action metadata: {action}")
        if len(edits) != 1 or edits[0].get("newText") != expected_replacement:
            raise RuntimeError(f"unexpected return conversion edit: {edits}")
        if edits[0].get("range") != diagnostics[0].get("range"):
            raise RuntimeError("the return conversion edit does not replace the conflicting value")

        repaired_text = apply_change_to_text(
            document.read_text(), {"range": edits[0]["range"], "text": edits[0]["newText"]}
        )
        session.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": repaired_text}],
            },
        )
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        repaired_diagnostics = [
            message["params"]["diagnostics"]
            for message in session.server_notifications
            if message.get("method") == "textDocument/publishDiagnostics"
            and message.get("params", {}).get("uri") == uri
        ]
        if not repaired_diagnostics or repaired_diagnostics[-1]:
            raise RuntimeError("applying the return conversion quick fix did not clear diagnostics")
    finally:
        session.close()

    print("Incompatible return diagnostics provide an explicit conversion quick fix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
