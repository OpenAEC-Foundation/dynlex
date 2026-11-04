#!/usr/bin/env python3
"""Outline ranges cover authored sections, including their nested execution bodies."""
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    uri = to_file_uri(root / "tests/lsp/symbols.dl")
    source = '''to outer:
    to inner:
        @intrinsic("discard", "😀é")
    inner
outer

function another:
    execute:
        @intrinsic("discard", 2)
another

an integer means: @intrinsic("type", "int", 32)
class:
    patterns:
        crate
    members:
        size as an integer
'''
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})
        symbols = session.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        lines = source.splitlines()

        def span(symbol, first, last):
            assert symbol["range"]["start"]["line"] == first, symbol
            assert symbol["range"]["end"] == {
                "line": last, "character": len(lines[last].encode("utf-16-le")) // 2,
            }, symbol
            assert symbol["selectionRange"]["start"]["line"] >= first
            assert symbol["selectionRange"]["end"]["line"] <= last

        outer = next(symbol for symbol in symbols if symbol["name"] == "outer")
        span(outer, 0, 3)
        span(outer["children"][0], 1, 2)
        span(next(symbol for symbol in symbols if symbol["name"] == "another"), 6, 8)
        span(next(symbol for symbol in symbols if symbol["name"] == "crate"), 12, 16)
        updated = source.replace('    inner\nouter', '    @intrinsic("discard", "🌽")\nouter')
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                                  "contentChanges": [{"text": updated}]})
        lines = updated.splitlines()
        symbols = session.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
        span(next(symbol for symbol in symbols if symbol["name"] == "outer"), 0, 3)

        deferred = (root / "tests/required/deferred_data_sections_nested/main.dl").read_text()
        deferred = deferred.replace("    run body:", '    to nested helper:\n        @intrinsic("discard", 0)\n    run body:')
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3},
                                                  "contentChanges": [{"text": deferred}]})
        symbols = session.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})

        def authored_symbols(entries):
            for symbol in entries:
                assert not symbol["name"].startswith("__section_body_"), symbol
                authored_symbols(symbol.get("children", []))

        authored_symbols(symbols)
        assert any(symbol["name"] == "nested helper" for symbol in symbols), symbols
        rendered = session.request("dynlex/renderSemanticTokens", {"uri": uri})
        body_line = deferred.splitlines().index("    run body:")
        header = rendered.splitlines()[body_line]
        assert "<section>" in header, header
        assert all(tag not in header for tag in ("<patternDefinition>", "<variable>", "<type>")), header
    finally:
        session.close()
    print("Outline ranges cover authored bodies and UTF-16 edits; deferred callbacks keep generated headers hidden.")


if __name__ == "__main__":
    main()
