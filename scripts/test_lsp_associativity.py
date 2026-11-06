#!/usr/bin/env python3
"""Keep editor validation and completion aligned with associativity syntax."""
from pathlib import Path
import tempfile

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri
from test_lsp_config import published_diagnostics

root = Path(__file__).resolve().parent.parent
text = '''dynlex options:
    child sections:
        associativity: "grouping"
    associativity:
        left: "forward"
        right: "backward"

    messages:
        invalid associativity: "A direction is required"
        associativity requires binary infix: "A binary infix is required"
        conflicting associativity: "The directions conflict"
        conflicting associativity related: "The other direction is here"
'''
with tempfile.TemporaryDirectory(prefix="dynlex-lsp-associativity-") as directory:
    path = Path(directory) / "config.dl"
    path.write_text(text)
    uri = to_file_uri(path)
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify("textDocument/didOpen", {
            "textDocument": {"uri": uri, "languageId": "dynlex", "version": 1, "text": text},
        })
        result = session.request("textDocument/completion", {
            "textDocument": {"uri": uri}, "position": {"line": 6, "character": 8},
        })
        diagnostics = published_diagnostics(session, uri)
        assert diagnostics == [], diagnostics
        labels = {item["label"] for item in result["items"]}
        assert {'left: "left"', 'right: "right"'}.issubset(labels), labels

        session.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [{"text": text.replace('"backward"', '"forward"')}],
        })
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        messages = [item["message"] for item in published_diagnostics(session, uri)]
        assert messages == ["associativity directions must be distinct"], messages
    finally:
        session.close()
print("LSP associativity: passed")
