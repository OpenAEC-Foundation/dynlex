#!/usr/bin/env python3
"""Hover uses the selected inferred body, including parameter declarations and locals."""
import json
import pathlib

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests/lsp/hover_instances/main.dl"
    uri = to_file_uri(document)
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialized = session.request("initialize", {"rootUri": to_file_uri(root), "capabilities": {}})
        session.notify("initialized", {})
        legend = initialized["capabilities"]["semanticTokensProvider"]["legend"]
        constant_mask = 1 << legend["tokenModifiers"].index("constant")
        source = document.read_text()
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})

        def hover(line, word, expected):
            character = source.splitlines()[line].rindex(word) + 1
            result = session.request("textDocument/hover", {
                "textDocument": {"uri": uri}, "position": {"line": line, "character": character},
            })
            contents = result["contents"]
            text = contents["value"] if isinstance(contents, dict) else contents
            assert expected in text, (line, word, expected, result)

        hover(2, "value", "a 32-bit integer")
        hover(0, "value", "a 32-bit integer")
        source = source.replace("echo 7)", 'echo "updated")')
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                                  "contentChanges": [{"text": source}]})
        # An import-free string literal is a C byte pointer, not lib/string.dl's record.
        hover(2, "value", "a pointer to a byte")
        hover(0, "value", "a pointer to a byte")

        source = '''function echo {value}:
    execute:
        @intrinsic("store", copy, value)
        @intrinsic("return", copy)

@intrinsic("discard", echo 7)
@intrinsic("discard", echo "updated")
'''
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3},
                                                  "contentChanges": [{"text": source}]})
        entries = session.request("dynlex/instantiationsInDocument", {"uri": uri})
        header = next(entry for entry in entries if entry["range"]["start"]["line"] == 0)
        assert len(header["options"]) == 2, header
        for expected in ("a 32-bit integer", "a pointer to a byte"):
            option = next(option for option in header["options"] if expected in option["label"])
            session.notify("dynlex/selectInstantiation", {
                "selectionKey": header["selectionKey"], "instantiationKey": option["key"],
            })
            hover(0, "value", expected)
            hover(2, "value", expected)
            hover(2, "copy", expected)
            hover(3, "copy", expected)
        ownership = root / "tests/lsp/hover_ownership"
        for version, case in enumerate(json.loads((ownership / "expected.json").read_text()), start=4):
            source = (ownership / case["source"]).read_text()
            assert "import " not in source
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                      "contentChanges": [{"text": source}]})
            data = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})["data"]
            line = column = 0
            tokens = []
            for index in range(0, len(data), 5):
                delta_line, delta_column, length, kind, modifiers = data[index:index + 5]
                line += delta_line
                column = delta_column if delta_line else column + delta_column
                tokens.append((line, column, length, kind, modifiers))
            for point in case["points"]:
                position = {"line": point["line"],
                            "character": source.splitlines()[point["line"]].rindex(point["word"]) + 1}
                result = session.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": position})
                if "value" in point:
                    assert result["contents"] == {"kind": "markdown", "value": point["value"]}, (case, point, result)
                else:
                    # A mutable global's runtime value need not be constant in its callee.
                    expected_type = "type:\n\n```dynlex\n" + point["type"] + "\n```"
                    assert result["contents"]["value"].startswith(expected_type), (case, point, result)
                if "constant" in point:
                    token = next(token for token in tokens if token[0] == position["line"]
                                 and token[1] <= position["character"] < token[1] + token[2])
                    assert legend["tokenTypes"][token[3]] == "variable", token
                    assert bool(token[4] & constant_mask) == point["constant"], (case, point, token)
                    definition_mask = 1 << legend["tokenModifiers"].index("definition")
                    assert bool(token[4] & definition_mask) == point["definition"], (case, point, token)
    finally:
        session.close()
    print("Hover types and values follow lexical ownership and selected instances through source edits.")


if __name__ == "__main__":
    main()
