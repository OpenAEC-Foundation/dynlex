#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests" / "required" / "function_semantics_with_global_pattern_failure" / "main.dl"
    uri = to_file_uri(document)
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        source = document.read_text()
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": 1,
                    "text": source,
                }
            },
        )
        rendered = session.request("dynlex/renderSemanticTokens", {"uri": uri})

        unresolved_body_source = source.replace('@intrinsic("return", value)', "missing value")
        session.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": unresolved_body_source}],
            },
        )
        unresolved_body_rendered = session.request("dynlex/renderSemanticTokens", {"uri": uri})
    finally:
        session.close()

    if "<patternDefinition> squared</patternDefinition>" not in rendered:
        raise RuntimeError(f"resolved function literal was not highlighted: {rendered}")
    if "<variable>value</variable>" not in rendered:
        raise RuntimeError(f"resolved function parameter was not highlighted: {rendered}")
    if "<function>square x</function>" in rendered or "<variable>x</variable>" in rendered:
        raise RuntimeError(f"unresolved global pattern received semantic highlighting: {rendered}")
    if "<patternDefinition>" in unresolved_body_rendered or "<variable>" in unresolved_body_rendered:
        raise RuntimeError(
            "an uncommitted function-resolution attempt leaked semantic highlighting: "
            f"{unresolved_body_rendered}"
        )

    print("Function semantic tokens stop at the committed pattern-resolution boundary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
