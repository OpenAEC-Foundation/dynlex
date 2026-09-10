#!/usr/bin/env python3
"""Editor positions are UTF-16, including after non-BMP text and incremental edits."""
import pathlib
import re

from lsp_tokens import LspSession, default_server_path, to_file_uri


def units(text):
    return len(text.encode("utf-16-le")) // 2


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    session = LspSession(default_server_path(root), root, False, False)
    uri = to_file_uri(root / "tests/lsp/unicode.dl")
    source = 'to show {value} twice: @intrinsic("discard", value)\r\nshow "😀é中" twice\r\n'
    try:
        initialized = session.request("initialize", {"rootUri": to_file_uri(root), "capabilities": {}})
        session.notify("initialized", {})
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})

        def check(text):
            line_text = text.splitlines()[1]
            calls = session.request("dynlex/callExpressions", {"uri": uri})
            call = next(call for call in calls if call["range"]["start"]["line"] == 1)
            assert call["range"]["end"] == {"line": 1, "character": units(line_text)}, call
            data = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})["data"]
            lines, column, segments = 0, 0, []
            for offset in range(0, len(data), 5):
                dl, dc, length, kind, _ = data[offset:offset + 5]
                lines += dl
                column = dc if dl else column + dc
                encoded = text.splitlines()[lines].encode("utf-16-le")
                fragment = encoded[column * 2:(column + length) * 2].decode("utf-16-le")
                if lines == 1:
                    segments.append(fragment)
            assert "".join(segments) == line_text, segments
            tagged = session.request("dynlex/renderSemanticTokens", {"uri": uri})
            assert re.sub(r"</?\w+>", "", tagged) == text, tagged
            position = {"line": 1, "character": units(line_text) - 1}
            definition = session.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": position})
            assert definition["range"]["start"]["line"] == 0, definition
            hover = session.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": position})
            assert hover and hover["range"] == call["range"], hover

        check(source)
        assert initialized["capabilities"]["positionEncoding"] == "utf-16"
        # Replace after an emoji, then replace the emoji itself (two UTF-16 units).
        for version, start, end, replacement, old in [(2, 8, 10, "Å", "é中"), (3, 6, 8, "🌽🐔", "😀")]:
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                "contentChanges": [{"range": {"start": {"line": 1, "character": start},
                                               "end": {"line": 1, "character": end}}, "text": replacement}]})
            source = source.replace(old, replacement)
            assert session.request("dynlex/readDocument", {"uri": uri}) == source
            check(source)
        # Completion replacement ranges use the same units as cursor requests.
        prefix = 'show "🌽🐔Å" tw'
        source = source.splitlines()[0] + "\n" + prefix + "\n"
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 4},
                                                  "contentChanges": [{"text": source}]})
        completions = session.request("textDocument/completion", {"textDocument": {"uri": uri},
                                       "position": {"line": 1, "character": units(prefix)}})
        twice = next(item for item in completions["items"] if "twice" in item["label"])
        assert twice["textEdit"]["range"]["end"]["character"] == units(prefix), twice
        source = source.splitlines()[0] + '\nto example:\n    show "😀é中" twice\nexample\n'
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 5},
                                                  "contentChanges": [{"text": source}]})
        symbols = session.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        example = next(symbol for symbol in symbols if "example" in symbol["name"])
        assert example["range"]["end"] == {"line": 2, "character": units(source.splitlines()[2])}, example
        source = source.splitlines()[0] + '\nshow "😀é中" unknown\n'
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 6},
                                                  "contentChanges": [{"text": source}]})
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        diagnostics = [n["params"]["diagnostics"] for n in session.server_notifications
                       if n.get("method") == "textDocument/publishDiagnostics" and n["params"]["uri"] == uri][-1]
        assert diagnostics[0]["range"]["end"] == {"line": 1, "character": units(source.splitlines()[1])}, diagnostics
    finally:
        session.close()
    print("Unicode semantic tokens, source ranges, hover, definitions, completions and incremental edits passed.")


if __name__ == "__main__":
    main()
