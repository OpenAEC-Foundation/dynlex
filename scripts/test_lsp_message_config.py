#!/usr/bin/env python3
"""Message overrides and completions use the compiler's configuration data."""
import pathlib
import subprocess
import tempfile

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-message-config-") as directory:
        work = pathlib.Path(directory)
        config = work / "config.dl"
        entry = work / "main.dl"
        entry.write_text('@intrinsic("discard", 1)\n')
        uri = to_file_uri(config)
        cases = (
            'dynlex options:\n    messages:\n        ambiguous operand grouping: "Updated {expression}"\n',
            'dynlex options:\n    messages:\n        custom diagnostic:\n'
            '            message: "Custom message"\n            custom variant: ""\n',
            'dynlex options:\n    messages:\n        ambiguous operand grouping: ""\n',
        )
        session = LspSession(default_server_path(root), root, False, False)
        try:
            initialize_session(session, root)

            def change(text, version):
                method = "textDocument/didOpen" if version == 1 else "textDocument/didChange"
                params = ({"textDocument": {"uri": uri, "languageId": "dynlex", "version": version, "text": text}}
                          if version == 1 else {"textDocument": {"uri": uri, "version": version},
                                                "contentChanges": [{"text": text}]})
                session.notify(method, params)
                session.request("dynlex/readDocument", {"uri": uri})
                messages = [n["params"]["diagnostics"] for n in session.server_notifications
                            if n.get("method") == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri]
                assert messages, "config diagnostics were not published"
                return messages[-1]

            for version, text in enumerate(cases, 1):
                config.write_text(text)
                result = subprocess.run([str(default_server_path(root)), str(entry), "--no-incremental", "--emit-llvm",
                                         "-o", str(work / "main.ll")], capture_output=True, text=True, timeout=10)
                assert result.returncode == 0, result.stderr
                diagnostics = change(text, version)
                assert not diagnostics, diagnostics

            text = 'dynlex options:\n    messages:\n        custom diagnostic:\n            detail:\n'
            diagnostics = change(text, 4)
            assert [d["message"] for d in diagnostics] == [
                "config entry 'messages.custom diagnostic.detail' must define a value"
            ], diagnostics

            partial = 'dynlex options:\n    messages:\n        ambig'
            change(partial, 5)
            result = session.request("textDocument/completion", {"textDocument": {"uri": uri},
                                      "position": {"line": 2, "character": len('        ambig')}})
            assert any(item["label"].startswith("ambiguous operand grouping:") for item in result["items"]), result

            variants = 'dynlex options:\n    messages:\n        ambiguous single word reference:\n            '
            change(variants, 6)
            result = session.request("textDocument/completion", {"textDocument": {"uri": uri},
                                      "position": {"line": 3, "character": 12}})
            assert {item["label"].split(":", 1)[0] for item in result["items"]} >= {"message", "related parameter"}, result

            builtin_uri = to_file_uri(root / "lib/config.dl")
            builtin = 'dynlex options:\n    messages:\n        editor supplied diagnostic: "Value with \\\"quotes\\\""\n'
            session.notify("textDocument/didOpen", {"textDocument": {"uri": builtin_uri, "languageId": "dynlex",
                                                                    "version": 1, "text": builtin}})
            change('dynlex options:\n    messages:\n        editor supplied', 7)
            result = session.request("textDocument/completion", {"textDocument": {"uri": uri},
                                      "position": {"line": 2, "character": len('        editor supplied')}})
            assert [item["label"] for item in result["items"]] == [
                'editor supplied diagnostic: "Value with \\\"quotes\\\""'
            ], result
            session.notify("textDocument/didClose", {"textDocument": {"uri": builtin_uri}})
        finally:
            session.close()
    print("Message overrides, variants, empty values and data-driven completions match compiler configuration.")


if __name__ == "__main__":
    main()
