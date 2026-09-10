#!/usr/bin/env python3
"""Nested literal completions must survive competing enclosing function parses."""
import pathlib
from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    source = '''to show {value}: @intrinsic("discard", value)
red apple means: 1
red apricot means: 2
{value} is ripe means: @intrinsic("equal", value, value)
show red apple
'''
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        uri = to_file_uri(root / "tests/lsp/nested-completion.dl")
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        for version, prefix in enumerate(["show red ap", "show red ", "show red apple is r"], 2):
            current = source.rsplit("show red apple", 1)[0] + prefix
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                    "contentChanges": [{"text": current}]})
            response = session.request("textDocument/completion", {
                "textDocument": {"uri": uri}, "position": {"line": 4, "character": len(prefix)},
            })
            items = {item["label"]: item for item in response["items"]}
            expected = {"ripe"} if prefix.endswith("is r") else {"apple", "apricot"}
            assert expected <= items.keys(), (prefix, items)
            for word in expected:
                assert items[word].get("command", {}).get("command") == "editor.action.triggerSuggest", items[word]
                edit = items[word]["textEdit"]
                assert edit["range"]["start"]["line"] == edit["range"]["end"]["line"] == 4
                completed = prefix[:edit["range"]["start"]["character"]] + edit["newText"] + prefix[edit["range"]["end"]["character"]:]
                assert completed == ("show red apple is ripe" if word == "ripe" else "show red " + word), completed

        # Finishing the short overload must retain the longer overload's suffix.
        declarations = '''fertilizer means: 1
heap means: 2
to [grab|take|collect|pick up] {items}: @intrinsic("discard", items)
to [grab|take|collect|pick up] {items} from {place}: @intrinsic("discard", @intrinsic("add", items, place))
'''
        for version, verb in enumerate(["grab", "take", "collect", "pick up"], 5):
            prefix = verb + " fertilizer"
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                                    "contentChanges": [{"text": declarations + prefix}]})
            response = session.request("textDocument/completion", {
                "textDocument": {"uri": uri}, "position": {"line": 4, "character": len(prefix)},
            })
            items = {item["label"]: item for item in response["items"]}
            assert " from " in items, (prefix, items)
            edit = items[" from "]["textEdit"]
            assert edit["range"]["start"] == edit["range"]["end"] == {"line": 4, "character": len(prefix)}
            assert prefix + edit["newText"] == verb + " fertilizer from "
            assert items[" from "]["command"]["command"] == "editor.action.triggerSuggest"
    finally:
        session.close()
    print("Nested literal alternatives and enclosing suffixes keep their completion edits.")


if __name__ == "__main__":
    main()
