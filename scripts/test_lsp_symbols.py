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
    finally:
        session.close()
    print("Outline ranges cover shorthand, nested and explicit bodies, classes and edits in UTF-16.")


if __name__ == "__main__":
    main()
