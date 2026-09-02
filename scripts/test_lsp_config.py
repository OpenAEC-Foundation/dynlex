#!/usr/bin/env python3
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def published_diagnostics(session: LspSession, uri: str) -> list[dict]:
    matching = [
        notification["params"]["diagnostics"]
        for notification in session.server_notifications
        if notification.get("method") == "textDocument/publishDiagnostics"
        and notification.get("params", {}).get("uri") == uri
    ]
    if not matching:
        raise AssertionError("the config document did not publish diagnostics")
    return matching[-1]


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    valid_text = """dynlex options:
    shorthand definitions:
        action: "to"
        value: "to get"
        replacement: "means"

"""
    invalid_text = """dynlex options:
    shorthand definitions:
        action: "means"
        replacement: "means"
"""
    leading_collision_text = """dynlex options:
    function: "perform"
    shorthand definitions:
        action: "perform"
"""
    child_collision_text = """dynlex options:
    shorthand definitions:
        replacement: "replacement"
"""

    with tempfile.TemporaryDirectory(prefix="dynlex-lsp-config-") as directory:
        config_path = pathlib.Path(directory) / "config.dl"
        config_path.write_text(valid_text, encoding="utf-8")
        uri = to_file_uri(config_path)
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
                        "text": valid_text,
                    }
                },
            )
            completion = session.request(
                "textDocument/completion",
                {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}},
            )
            labels = {item["label"] for item in completion["items"]}
            expected_labels = {'action: "to"', 'value: "to get"', 'replacement: "means"'}
            if not expected_labels.issubset(labels):
                raise AssertionError(f"missing shorthand config completions: {sorted(expected_labels - labels)!r}")
            if published_diagnostics(session, uri):
                raise AssertionError("valid multiword shorthand config produced diagnostics")

            session.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": invalid_text}],
                },
            )
            session.request(
                "textDocument/semanticTokens/full",
                {"textDocument": {"uri": uri}},
            )
            diagnostics = published_diagnostics(session, uri)
            if [diagnostic["message"] for diagnostic in diagnostics] != [
                "shorthand definition phrases must be distinct"
            ]:
                raise AssertionError(f"unexpected duplicate shorthand diagnostics: {diagnostics!r}")

            for version, changed_text, expected_message in (
                (
                    3,
                    leading_collision_text,
                    "shorthand definition phrase 'perform' conflicts with structural keyword 'perform'",
                ),
                (
                    4,
                    child_collision_text,
                    "shorthand definition phrase 'replacement' conflicts with structural keyword 'replacement'",
                ),
            ):
                session.notify(
                    "textDocument/didChange",
                    {
                        "textDocument": {"uri": uri, "version": version},
                        "contentChanges": [{"text": changed_text}],
                    },
                )
                session.request(
                    "textDocument/semanticTokens/full",
                    {"textDocument": {"uri": uri}},
                )
                messages = [diagnostic["message"] for diagnostic in published_diagnostics(session, uri)]
                if messages != [expected_message]:
                    raise AssertionError(f"unexpected shorthand collision diagnostics: {messages!r}")
        finally:
            session.close()


if __name__ == "__main__":
    main()
