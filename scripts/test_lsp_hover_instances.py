#!/usr/bin/env python3
"""Hover uses the selected inferred body, including parameter declarations and locals."""
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests/lsp/hover_instances/main.dl"
    uri = to_file_uri(document)
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
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
    finally:
        session.close()
    print("Hover types follow selected instances for parameters, locals, and source edits.")


if __name__ == "__main__":
    main()
