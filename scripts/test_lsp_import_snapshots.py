#!/usr/bin/env python3
"""CWD-relative imports consume unsaved editor snapshots, including longer edits."""
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, to_file_uri


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-import-snapshots-") as directory:
        work = pathlib.Path(directory)
        entry = work / "entry/main.dl"
        entry.parent.mkdir()
        entry.write_text('import helper.dl\n@intrinsic("discard", chosen)\n')
        helper = work / "helper.dl"
        helper.write_text("chosen means: 3\n")
        uri = to_file_uri(helper)
        session = LspSession(default_server_path(root), work, False, False)
        try:
            result = session.request("initialize", {"rootUri": to_file_uri(work), "capabilities": {}})
            number = result["capabilities"]["semanticTokensProvider"]["legend"]["tokenTypes"].index("number")
            session.notify("initialized", {})
            for path in (entry, helper):
                session.notify("textDocument/didOpen", {"textDocument": {
                    "uri": to_file_uri(path), "languageId": "dynlex", "version": 1, "text": path.read_text(),
                }})
            session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                                     "contentChanges": [{"text": "chosen means: 10\n"}]})
            data = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})["data"]
            lengths = [data[offset + 2] for offset in range(0, len(data), 5) if data[offset + 3] == number]
            assert lengths == [2], lengths
            assert helper.read_text() == "chosen means: 3\n"
        finally:
            session.close()
    print("CWD-relative imports read unsaved editor text and preserve updated token ranges.")


if __name__ == "__main__":
    main()
