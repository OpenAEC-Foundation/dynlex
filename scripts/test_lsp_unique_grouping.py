#!/usr/bin/env python3
"""Single structural groupings infer inside their existing enclosing trial."""
import json
from pathlib import Path
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-unique-grouping-") as directory:
        work = Path(directory)
        path = work / "main.dl"
        uri = to_file_uri(path)
        source = ('to get identity {value}:\n    @intrinsic("return", value)\n'
                  'to use {value}: @intrinsic("discard", identity value)\nuse 1\n')
        path.write_text(source)

        def start(trace=False):
            args = ("--stdio", "--lsp-trace=" + str(work / "trace.jsonl")) if trace else ("--stdio",)
            session = LspSession(default_server_path(root), work, False, False, args)
            session.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {},
                "initializationOptions": {"dynlex": {"analysisProfiles": [
                    {"target": "cpu"}, {"target": "spirv", "shaderStage": "fragment"}]}}})
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
            initial = result(session)
            assert not initial[1], initial[1]
            rows = [json.loads(n["params"]["message"].removeprefix("incremental-lsp: "))
                    for n in session.server_notifications if n.get("method") == "window/logMessage"
                    and n["params"]["message"].startswith("incremental-lsp: ")]
            assert rows[-1].get("unique_grouping_trials_skipped", 0) > 0, rows
            ambiguous = ('to get a minus b:\n    @intrinsic("return", @intrinsic("subtract", a, b))\n'
                         '@intrinsic("discard", 3 minus 0 minus 2)\n')
            cases = [(source.replace("use 1", "use 2"), False),
                     (source.replace("identity value", '@intrinsic("multiply", value, "text")'), True),
                     (source, False), (ambiguous, False)]
            for version, (text, error) in enumerate(cases, 2):
                session.server_notifications.clear()
                session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                         "contentChanges": [{"text": text}]})
                actual = result(session)
                fresh = start()
                try:
                    open_document(fresh, text)
                    assert actual == result(fresh), "edited and fresh tokens/diagnostics differ"
                finally:
                    fresh.close()
                assert any(d["severity"] == 1 for d in actual[1]) == error, actual[1]
                if text == ambiguous:
                    assert any("multiple valid operand groupings" in d["message"] for d in actual[1]), actual[1]
        finally:
            session.close()
    print("Unique grouping inference preserves errors, ambiguity and fresh editor results.")


if __name__ == "__main__":
    main()
