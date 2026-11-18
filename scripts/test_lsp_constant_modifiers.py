#!/usr/bin/env python3
"""Constant modifiers follow reference identity and the current editor snapshot."""
import pathlib

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    uri = to_file_uri(root / "tests/lsp/constant-modifiers.dl")
    source = '''to shadow {value}:
    @intrinsic("store", fixed, value)
    @intrinsic("discard", fixed)
@intrinsic("store", fixed, 7)
@intrinsic("store", moving, @intrinsic("command line argument count"))
@intrinsic("discard", fixed)
@intrinsic("discard", moving)
shadow moving
'''
    session = LspSession(default_server_path(root), root, False, False)
    try:
        result = session.request("initialize", {"rootUri": to_file_uri(root), "capabilities": {}})
        legend = result["capabilities"]["semanticTokensProvider"]["legend"]
        session.notify("initialized", {})
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})

        def variables(text):
            data = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})["data"]
            lines = text.splitlines()
            output = {}
            line = column = 0
            for offset in range(0, len(data), 5):
                dl, dc, length, kind, modifiers = data[offset:offset + 5]
                line += dl
                column = dc if dl else column + dc
                if legend["tokenTypes"][kind] == "variable":
                    names = {name for bit, name in enumerate(legend["tokenModifiers"]) if modifiers & (1 << bit)}
                    output[line, lines[line][column:column + length].strip()] = names
            return output

        original = variables(source)
        assert "constant" in original[5, "fixed"], original
        assert "constant" not in original[2, "fixed"], original
        assert "constant" not in original[6, "moving"], original
        assert variables(source) == original
        updated = source.replace('fixed, 7)', 'fixed, @intrinsic("command line argument count"))')
        for version, text in ((2, updated), (3, source)):
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                     "contentChanges": [{"text": text}]})
            current = variables(text)
            assert ("constant" in current[5, "fixed"]) == (version == 3), current
            assert "constant" not in current[2, "fixed"], current
            assert "constant" not in current[6, "moving"], current
            if version == 3:
                assert current == original
    finally:
        session.close()
    print("Constant modifiers preserve scope identity, repeated queries and edited values.")


if __name__ == "__main__":
    main()
