#!/usr/bin/env python3
"""Editor source changes reuse matcher products with current dependency proofs."""
import json
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    profiles = {"dynlex": {"analysisProfiles": [{"target": "cpu"}, {"target": "spirv", "shaderStage": "fragment"}]}}
    with tempfile.TemporaryDirectory(prefix="dynlex-matcher-") as directory:
        work = pathlib.Path(directory)
        entry = work / "main.dl"
        helper = work / "helper.dl"
        helper.write_text('to imported {value}: @intrinsic("discard", value)\n')
        source = ('to stable {value}: @intrinsic("discard", value)\n'
                  'to edited {value}: @intrinsic("discard", value)\nstable 1\nedited 2\n')
        entry.write_text(source)
        uri = to_file_uri(entry)

        def start(trace=False):
            args = ("--stdio", "--lsp-trace=" + str(work / "trace.jsonl")) if trace else ("--stdio",)
            session = LspSession(default_server_path(root), work, False, False, args)
            session.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {}, "initializationOptions": profiles})
            session.notify("initialized", {})
            return session

        def open_document(session, text):
            session.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "dynlex", "version": 1, "text": text}})

        def result(session):
            tokens = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
            diagnostics = [n["params"]["diagnostics"] for n in session.server_notifications
                           if n.get("method") == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri]
            assert diagnostics, "main diagnostics were not published"
            return tokens, diagnostics[-1]

        session = start(True)
        try:
            open_document(session, source)
            result(session)

            def counts():
                rows = [json.loads(n["params"]["message"].removeprefix("incremental-lsp: "))
                        for n in session.server_notifications if n.get("method") == "window/logMessage"
                        and n["params"]["message"].startswith("incremental-lsp: ")]
                assert rows
                row = rows[-1]
                assert "matcher_hits" in row, "the LSP does not reuse native matcher products"
                return row

            cold = counts()
            assert cold["matcher_hits"] == 0 and cold["matcher_bytes"] > 0, cold
            version = 1

            def change(text, error=False):
                nonlocal version
                version += 1
                session.server_notifications.clear()
                session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                         "contentChanges": [{"text": text}]})
                actual = result(session)
                fresh = start()
                try:
                    open_document(fresh, text)
                    assert actual == result(fresh), "incremental tokens or diagnostics differ from fresh analysis"
                finally:
                    fresh.close()
                assert any(d["severity"] == 1 for d in actual[1]) == error, actual[1]
                return counts()

            edited = source.replace('to edited {value}: @intrinsic("discard", value)',
                                    'to edited {value}: @intrinsic("discard", @intrinsic("add", value, 1))')
            reused = change(edited)
            assert reused["matcher_hits"] > 0, reused
            assert reused["matcher_computations"] < cold["matcher_computations"], (cold, reused)
            specific = 'to stable 1: @intrinsic("discard", 9)\n'
            change(edited + specific)
            change(edited)
            implicit = 'to implicit item: @intrinsic("discard", item)\nimplicit 2\n'
            change(edited + implicit)
            change(edited + implicit.replace('"discard", item', '"discard", 0'), True)
            change(edited + implicit)
            change(edited + 'mystery 3\n', True)
            change(edited + 'mystery 3\nto mystery {value}: @intrinsic("discard", value)\n')
            imported = 'import helper.dl\n' + edited + 'imported 1\n'
            change(imported)
            helper.write_text('to imported item: @intrinsic("discard", 0)\n')
            change(imported + '# provider changed\n', True)
            helper.write_text('to imported {value}: @intrinsic("discard", value)\n')
            change(imported)
            grown = change(edited + specific + implicit)
            pruned = change(source)
            assert pruned["matcher_bytes"] < grown["matcher_bytes"], (grown, pruned)
            failed_sizes = [change(source + f'unknown_{index:04d} 3\n', True)["matcher_bytes"] for index in range(61)]
            assert max(failed_sizes) <= 3 * pruned["matcher_bytes"], failed_sizes
            assert change(source)["matcher_bytes"] == pruned["matcher_bytes"]
        finally:
            session.close()
    print("LSP matcher reuse preserves overloads, negative lookups, classification, imports, profiles and source ranges.")


if __name__ == "__main__":
    main()
