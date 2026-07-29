#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests" / "lsp" / "instantiation_labels" / "main.dl"
    uri = to_file_uri(document)
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": 1,
                    "text": document.read_text(),
                }
            },
        )
        entries = session.request(
            "dynlex/instantiationsInDocument",
            {"uri": uri},
        )
    finally:
        session.close()

    expected = "send {string:message} to {user:user}"
    labels = {
        option["label"]
        for entry in entries
        for option in entry["options"]
    }
    if expected not in labels:
        raise RuntimeError(f"expected inferred instance label {expected!r}, received {sorted(labels)!r}")

    print(f"Inferred instance label includes parameter types: {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
