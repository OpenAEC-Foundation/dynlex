#!/usr/bin/env python3
"""Matcher products survive moving an unchanged source from disk into the editor."""
import json
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex provider identity ") as directory:
        work = pathlib.Path(directory)
        entry, helper = work / "main.dl", work / "helper.dl"
        helper_text = ('to stable {value}: @intrinsic("discard", value)\n'
                       'to edited {value}: @intrinsic("discard", value)\n')
        main_text = 'import helper.dl\nstable 1\nedited 2\n'
        entry.write_text(main_text)
        helper.write_text(helper_text)
        uri = to_file_uri(entry)

        def start(trace=False):
            args = ("--stdio", "--lsp-trace=" + str(work / "trace.jsonl")) if trace else ("--stdio",)
            s = LspSession(default_server_path(root), work, False, False, args)
            s.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {}, "initializationOptions": {"dynlex": {
                "analysisProfiles": [{"target": "cpu"}, {"target": "spirv", "shaderStage": "fragment"}]}}})
            s.notify("initialized", {})
            s.diagnostic_state = {uri: [], to_file_uri(helper): []}
            return s

        def open_document(s, path, text):
            s.notify("textDocument/didOpen", {"textDocument": {"uri": to_file_uri(path), "languageId": "dynlex",
                                                                    "version": 1, "text": text}})

        def result(s):
            tokens = {path.name: s.request("textDocument/semanticTokens/full", {"textDocument": {"uri": to_file_uri(path)}})
                      for path in (entry, helper)}
            reports = [n["params"] for n in s.server_notifications if n.get("method") == "textDocument/publishDiagnostics"]
            assert any(report["uri"] == uri for report in reports), "current diagnostics were not published"
            for report in reports:
                if report["uri"] in s.diagnostic_state:
                    s.diagnostic_state[report["uri"]] = report["diagnostics"]
            return tokens, s.diagnostic_state.copy()

        s = start(True)
        try:
            open_document(s, entry, main_text)
            assert not any(result(s)[1].values())
            open_document(s, helper, helper_text)
            result(s)
            version = 1

            def change(text, error=False):
                nonlocal version
                version += 1
                s.server_notifications.clear()
                s.notify("textDocument/didChange", {"textDocument": {"uri": to_file_uri(helper), "version": version},
                                                   "contentChanges": [{"text": text}]})
                actual = result(s)
                fresh = start()
                try:
                    open_document(fresh, helper, text)
                    open_document(fresh, entry, main_text)
                    expected = result(fresh)
                    assert actual == expected, (actual, expected)
                finally:
                    fresh.close()
                assert any(d["severity"] == 1 for values in actual[1].values() for d in values) == error, actual[1]
                rows = [json.loads(n["params"]["message"].removeprefix("incremental-lsp: "))
                        for n in s.server_notifications if n.get("method") == "window/logMessage"
                        and n["params"]["message"].startswith("incremental-lsp: ")]
                return next(r for r in reversed(rows) if r["main_uri"] == uri)

            first = change('# move source lines\n' + helper_text)
            assert first["matcher_computations"] == 0 and first["matcher_hits"] > 0, first
            assert first["contexts_evaluated"] == 2, first
            changed = helper_text.replace('"discard", value)\n', '"discard", @intrinsic("add", value, 1))\n', 1)
            assert change(changed)["matcher_hits"] > 0
            change(helper_text + 'to stable 1: @intrinsic("discard", 7)\n')
            change(helper_text)
            implicit = 'to implicit item: @intrinsic("discard", item)\nimplicit 3\n'
            change(helper_text + implicit)
            change(helper_text + implicit.replace('"discard", item', '"discard", 0'), True)
            change(helper_text + implicit)
        finally:
            s.close()
    print("Matcher identity survives disk/editor transitions, body edits, overloads and parameter cascades.")


if __name__ == "__main__":
    main()
