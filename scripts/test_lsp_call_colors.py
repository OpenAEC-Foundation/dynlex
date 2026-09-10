#!/usr/bin/env python3
"""Exercise resolved nesting, shared palette, and edit preservation through real LSP."""
import json
import pathlib

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    palette = json.loads((root / "shared/call-colors.json").read_text())
    source = '''one means: 1
{value} doubled means: @intrinsic("add", value, value)
the sum of {left} and {right} means: @intrinsic("add", left, right)
to show {value}: @intrinsic("discard", value)
show one doubled doubled
show the sum of one and one
show one doubled doubled doubled doubled doubled doubled doubled
show 42
'''
    session = LspSession(default_server_path(root), root, False, False)
    try:
        result = session.request("initialize", {"rootUri": to_file_uri(root), "capabilities": {}})
        legend = result["capabilities"]["semanticTokensProvider"]["legend"]
        expected_modifiers = [f"callDepth{i}" for i in range(len(palette))]
        assert legend["tokenModifiers"] == ["definition", "constant"] + expected_modifiers
        session.notify("initialized", {})
        uri = to_file_uri(root / "tests/lsp/call-colors.dl")
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})

        def tokens(text):
            data = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})["data"]
            lines = text.splitlines()
            output = []
            line = column = 0
            for offset in range(0, len(data), 5):
                dl, dc, length, kind, bits = data[offset:offset + 5]
                line += dl
                column = dc if dl else column + dc
                output.append((line, lines[line][column:column + length].strip(), legend["tokenTypes"][kind],
                               [name for bit, name in enumerate(legend["tokenModifiers"]) if bits & (1 << bit)]))
            return output

        def calls(output, line):
            return [(text, mods) for row, text, kind, mods in output if row == line and kind == "function"]

        original = tokens(source)
        assert calls(original, 4) == [("show", ["callDepth0"]), ("one", ["callDepth3"]),
                                       ("doubled", ["callDepth2"]), ("doubled", ["callDepth1"])]
        assert calls(original, 5) == [("show", ["callDepth0"]), ("the sum of", ["callDepth1"]),
                                       ("one", ["callDepth2"]), ("and", ["callDepth1"]),
                                       ("one", ["callDepth2"])]
        deep = calls(original, 6)
        assert deep == [("show", ["callDepth0"]), ("one", [f"callDepth{8 % len(palette)}"])] + [
            ("doubled", [f"callDepth{depth % len(palette)}"]) for depth in range(7, 0, -1)]
        assert (7, "42", "number", []) in original
        assert all(not any(m.startswith("callDepth") for m in mods)
                   for _, _, kind, mods in original if kind in ["variable", "patternDefinition", "number", "string"])
        updated = source.replace("show 42", "show 43")
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                                "contentChanges": [{"text": updated}]})
        assert calls(tokens(updated), 4) == calls(original, 4)

        farm = """import lib/farm_challenge.dl
walk to the nearest tree
if i can reach it:
    chop it down
if it's still there:
    store 2 logs
store everything
water the nearest thirsty crop
"""
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3},
                                                "contentChanges": [{"text": farm}]})
        resolved = tokens(farm)
        assert calls(resolved, 1) == [("walk to", ["callDepth0"]), ("the nearest", ["callDepth1"]), ("tree", ["callDepth2"])]
        assert calls(resolved, 2) == [("i can reach", ["callDepth0"]), ("it", ["callDepth1"])]
        assert calls(resolved, 3) == [("chop", ["callDepth0"]), ("it", ["callDepth1"]), ("down", ["callDepth0"])]
        assert calls(resolved, 4) == [("it", ["callDepth1"]), ("'s still there", ["callDepth0"])]
        assert (5, "2", "number", []) in resolved
        assert ("logs", ["callDepth2"]) in calls(resolved, 5)
        assert calls(resolved, 6) == [("store", ["callDepth0"]), ("everything", ["callDepth1"])]
        assert calls(resolved, 7) == [("water", ["callDepth0"]), ("the nearest", ["callDepth1"]), ("thirsty crop", ["callDepth2"])]
    finally:
        session.close()
    print("Nested calls, siblings, palette wrap, literal styles, and edits retain compiler-derived colors.")


if __name__ == "__main__":
    main()
