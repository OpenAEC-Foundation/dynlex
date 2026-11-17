#!/usr/bin/env python3
"""Configured comment edits retain semantics and publish current lexical tokens."""
import json
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    for prefix in ("#", "//"):
        with tempfile.TemporaryDirectory(prefix="dynlex-comments-") as directory:
            work = pathlib.Path(directory)
            (work / "config.dl").write_text('dynlex options:\n    comment: "' + prefix + '"\n')
            entry, helper = work / "main.dl", work / "helper.dl"
            helper.write_text('to stable {value}: @intrinsic("discard", value) ' + prefix + ' initial\n')
            source = ('import helper.dl\n'
                      '@intrinsic("discard", "' + prefix + ' literal")\n'
                      'stable 1\n\n')
            entry.write_text(source)
            texts = {entry: source}
            uri = to_file_uri(entry)

            def start(trace=False):
                args = ("--stdio", "--lsp-trace=" + str(work / "trace.jsonl")) if trace else ("--stdio",)
                session = LspSession(default_server_path(root), work, False, False, args)
                session.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {}, "initializationOptions": {
                    "dynlex": {"analysisProfiles": [{"target": "cpu"}, {"target": "spirv", "shaderStage": "fragment"}]}}})
                session.notify("initialized", {})
                return session

            def open_document(session, path, text):
                session.notify("textDocument/didOpen", {"textDocument": {"uri": to_file_uri(path), "languageId": "dynlex",
                                                                         "version": 1, "text": text}})

            def result(session):
                tokens = {str(path): session.request("textDocument/semanticTokens/full", {
                    "textDocument": {"uri": to_file_uri(path)}}) for path in texts}
                diagnostics = [n["params"]["diagnostics"] for n in session.server_notifications
                               if n.get("method") == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri]
                assert diagnostics, "missing current main diagnostics"
                code_line = next((index for index, line in enumerate(texts[entry].splitlines())
                                  if line.startswith("stable 1")), None)
                facts = None if code_line is None else [session.request(method, {
                    "textDocument": {"uri": uri}, "position": {"line": code_line, "character": 1}})
                    for method in ("textDocument/hover", "textDocument/definition")]
                return tokens, diagnostics[-1], facts

            session = start(True)
            try:
                open_document(session, entry, source)
                assert not result(session)[1], "initial fixture must compile"
                version = 1

                def change(path, text, reused, error=False):
                    nonlocal version
                    version += 1
                    session.server_notifications.clear()
                    if path not in texts:
                        open_document(session, path, text)
                    else:
                        session.notify("textDocument/didChange", {"textDocument": {"uri": to_file_uri(path), "version": version},
                                                                 "contentChanges": [{"text": text}]})
                    texts[path] = text
                    actual = result(session)
                    fresh = start()
                    try:
                        for target, content in texts.items():
                            open_document(fresh, target, content)
                        assert actual == result(fresh), "retained source facts differ from fresh editor analysis"
                    finally:
                        fresh.close()
                    assert any(d["severity"] == 1 for d in actual[1]) == error, actual[1]
                    rows = [json.loads(n["params"]["message"].removeprefix("incremental-lsp: "))
                            for n in session.server_notifications if n.get("method") == "window/logMessage"
                            and n["params"]["message"].startswith("incremental-lsp: ")]
                    row = next(r for r in reversed(rows) if r["main_uri"] == uri)
                    assert (row["contexts_reused"], row["contexts_evaluated"]) == ((2, 0) if reused else (0, 2)), row

                changed = source.replace('stable 1\n', 'stable 1 ' + prefix + ' longer café 🧪\n')
                change(entry, changed, True)
                change(entry, changed.replace('🧪', 'short'), True)
                change(entry, source[:-1] + prefix + ' comment on blank line\n', True)
                change(entry, source, True)
                change(helper, helper.read_text().replace('initial', 'a much longer imported comment'), True)
                change(helper, helper.read_text().replace('"discard", value', '"discard", 27'), False)
                change(helper, helper.read_text(), False)
                change(entry, source.replace(' literal', ' changed literal'), False)
                change(entry, source, False)
                change(entry, prefix + ' inserted line\n' + source, False)
                change(entry, source, False)
                change(entry, changed.replace('🧪', '🧪\a'), False, True)
                change(entry, source, False)
                change(entry, source.replace('stable 1', 'missing 1'), False, True)
                change(entry, source, False)
            finally:
                session.close()
    print("Editor comments preserve configured lexing, imports, coordinates, current tokens and error recovery.")


if __name__ == "__main__":
    main()
