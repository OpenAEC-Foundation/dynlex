#!/usr/bin/env python3
"""Configuration queries reuse semantic contexts and preserve their dependencies."""
import json
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex incremental config ") as directory:
        work = pathlib.Path(directory)
        entry = work / "app/main.dl"
        entry.parent.mkdir()
        config = work / "config.dl"
        helper = work / "shared.dl"
        helper.write_bytes(b"picked means: 10\r\n")
        source = ('import shared.dl\nmarker means: 1\nto apply {marker}:\n'
                  '    @intrinsic("discard", marker)\napply 2\n@intrinsic("discard", picked)\n')
        entry.write_text(source)
        text = ('dynlex options:\n    messages:\n        ambiguous single word reference:\n'
                '            message: "first {token}"\n            related parameter: "first parameter"\n')
        config.write_text(text)
        uri = to_file_uri(entry)
        session = LspSession(default_server_path(root), work, False, False,
                             ("--stdio", "--lsp-trace=" + str(work / "trace.jsonl")))
        try:
            session.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {},
                            "initializationOptions": {"dynlex": {"analysisProfiles": [
                                {"target": "cpu"}, {"target": "spirv", "shaderStage": "fragment"}]}}})
            session.notify("initialized", {})

            def open_document(path, content):
                session.notify("textDocument/didOpen", {"textDocument": {
                    "uri": to_file_uri(path), "languageId": "dynlex", "version": 1, "text": content,
                }})

            def change(path, content, version):
                session.server_notifications.clear()
                session.notify("textDocument/didChange", {"textDocument": {"uri": to_file_uri(path), "version": version},
                                                         "contentChanges": [{"text": content}]})
                session.request("dynlex/readDocument", {"uri": uri})

            def diagnostics():
                values = [n["params"]["diagnostics"] for n in session.server_notifications
                          if n.get("method") == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri]
                assert values, "main document diagnostics were not published"
                return values[-1]

            def work_counts(evaluated, reused):
                values = [json.loads(n["params"]["message"][len("incremental-lsp: "):])
                          for n in session.server_notifications if n.get("method") == "window/logMessage"
                          and n["params"]["message"].startswith("incremental-lsp: ")]
                values = [value for value in values if value["main_uri"] == uri]
                assert values, "the LSP did not expose analysis work counters"
                assert (values[-1]["contexts_evaluated"], values[-1]["contexts_reused"]) == (evaluated, reused), values

            open_document(config, text)
            open_document(entry, source)
            session.request("dynlex/readDocument", {"uri": uri})
            assert any(d["message"] == "first marker" for d in diagnostics()), diagnostics()
            work_counts(2, 0)
            original_tokens = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
            text = text.replace("first", "second")
            change(config, text, 2)
            work_counts(0, 2)
            result = next(d for d in diagnostics() if d["message"] == "second marker")
            assert any(note["message"] == "second parameter" for note in result["relatedInformation"])
            assert session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}) == original_tokens

            builtin = root / "lib/config.dl"
            builtin_text = builtin.read_text()
            lines = builtin_text.splitlines(keepends=True)
            start = next(i for i, line in enumerate(lines) if line.strip() == "ambiguous single word reference:")
            index = next(i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("related matched pattern:"))
            lines[index] = '            related matched pattern: "updated builtin note"\n'
            open_document(builtin, builtin_text)
            session.request("dynlex/readDocument", {"uri": uri})
            change(builtin, "".join(lines), 2)
            work_counts(0, 2)
            assert any(note["message"] == "updated builtin note" for d in diagnostics()
                       for note in d.get("relatedInformation", [])), diagnostics()
            session.server_notifications.clear()
            session.notify("textDocument/didClose", {"textDocument": {"uri": to_file_uri(builtin)}})
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(0, 2)
            assert not any(note["message"] == "updated builtin note" for d in diagnostics()
                           for note in d.get("relatedInformation", []))

            helper.write_bytes(b"picked means: 11\r\n")
            change(config, text.replace("second", "third"), 3)
            work_counts(2, 0)
            assert session.request("dynlex/readDocument", {"uri": to_file_uri(helper)}) == helper.read_bytes().decode("utf-8")
            shadow = entry.parent / "shared.dl"
            shadow.write_bytes(b"picked means: 20\r\n")
            change(config, text.replace("second", "fourth"), 4)
            work_counts(2, 0)
            assert session.request("dynlex/readDocument", {"uri": to_file_uri(shadow)}) == shadow.read_bytes().decode("utf-8")

            # A previously missing configuration can appear only in the editor.
            nearer = entry.parent / "config.dl"
            session.server_notifications.clear()
            open_document(nearer, text.replace("second", "nearer"))
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(0, 2)
            assert any(d["message"] == "nearer marker" for d in diagnostics())
            session.server_notifications.clear()
            session.notify("textDocument/didClose", {"textDocument": {"uri": to_file_uri(nearer)}})
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(0, 2)
            assert any(d["message"] == "fourth marker" for d in diagnostics())

            session.server_notifications.clear()
            open_document(shadow, "picked means: 21\n")
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(2, 0)
            session.server_notifications.clear()
            session.notify("textDocument/didClose", {"textDocument": {"uri": to_file_uri(shadow)}})
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(2, 0)
            assert session.request("dynlex/readDocument", {"uri": to_file_uri(shadow)}) == shadow.read_bytes().decode("utf-8")

            change(config, text.replace('dynlex options:\n', 'dynlex options:\n    comment: "//"\n'), 5)
            work_counts(2, 0)

            broken = 'dynlex options:\n    messages:\n        unfinished: "'
            change(config, broken, 6)
            work_counts(2, 0)
            assert any(d["severity"] == 1 for d in diagnostics())
            change(config, text, 7)
            work_counts(2, 0)
            assert any(d["message"] == "second marker" for d in diagnostics())

            error_source = (root / "tests/required/incompatible_return_type/main.dl").read_text()
            change(entry, error_source, 2)
            work_counts(2, 0)
            text = ('dynlex options:\n    messages:\n        incompatible return type:\n'
                    '            message: "changed return {actual_type}"\n'
                    '            related origin: "changed origin {type}"\n'
                    '            quick fix convert: "changed conversion {type}"\n')
            change(config, text, 8)
            work_counts(0, 2)
            result = next(d for d in diagnostics() if d["message"].startswith("changed return "))
            assert any(note["message"].startswith("changed origin ") for note in result["relatedInformation"]), result
            assert result["data"]["quickFixes"][0]["title"].startswith("changed conversion "), result
            actions = session.request("textDocument/codeAction", {"textDocument": {"uri": uri},
                                      "range": result["range"], "context": {"diagnostics": [result]}})
            assert len(actions) == 1 and actions[0]["title"].startswith("changed conversion "), actions

            change(entry, 'import invented.dl\n@intrinsic("discard", found)\n', 3)
            work_counts(2, 0)
            assert any(d["severity"] == 1 for d in diagnostics())
            invented = entry.parent / "invented.dl"
            session.server_notifications.clear()
            open_document(invented, "found means: 1\n")
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(2, 0)
            assert not any(d["severity"] == 1 for d in diagnostics()), diagnostics()
            session.server_notifications.clear()
            session.notify("textDocument/didClose", {"textDocument": {"uri": to_file_uri(invented)}})
            session.request("dynlex/readDocument", {"uri": uri})
            work_counts(2, 0)
            assert any(d["severity"] == 1 for d in diagnostics())
        finally:
            session.close()
    print("LSP configuration queries reuse all target profiles and preserve source, import, note and fix dependencies.")


if __name__ == "__main__":
    main()
