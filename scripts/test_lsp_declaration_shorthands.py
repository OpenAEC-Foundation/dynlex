#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def assert_multiline_inline_tokens(root: pathlib.Path) -> None:
    source = ('answer means: @intrinsic(\n'
              '    "add",\n'
              '    1,\n'
              '    2\n'
              ')\n'
              'to calculate: @intrinsic(\n'
              '    "add",\n'
              '    3,\n'
              '    4\n'
              ')\n')
    uri = to_file_uri(root / "tests/lsp/multiline-inline.dl")
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source,
        }})
        result = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        lines = source.splitlines()
        line = column = 0
        previous = (0, 0)
        locations = set()
        for offset in range(0, len(result["data"]), 5):
            delta_line, delta_column, length, _, _ = result["data"][offset:offset + 5]
            line += delta_line
            column = delta_column if delta_line else column + delta_column
            assert length > 0 and line < len(lines), (line, column, length)
            assert column + length <= len(lines[line]), (lines[line], column, length)
            assert (line, column) >= previous, (previous, line, column)
            previous = (line, column + length)
            locations.add((line, column, lines[line][column:column + length]))
        for line, text in [(1, '"add"'), (2, "1"), (3, "2"), (6, '"add"'), (7, "3"), (8, "4")]:
            assert (line, 4, text) in locations, (line, text, locations)
    finally:
        session.close()


def tagged_tokens(root: pathlib.Path, document: pathlib.Path) -> str:
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
                    "text": document.read_text(encoding="utf-8"),
                }
            },
        )
        return session.request("dynlex/renderSemanticTokens", {"uri": uri})
    finally:
        session.close()


def completion_labels(root: pathlib.Path, document: pathlib.Path, line: int, character: int) -> set[str]:
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
                    "text": document.read_text(encoding="utf-8"),
                }
            },
        )
        result = session.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": line, "character": character}},
        )
        return {item["label"] for item in result["items"]}
    finally:
        session.close()


def edited_completion_labels(root: pathlib.Path, document: pathlib.Path, line: int, replacement: str) -> set[str]:
    uri = to_file_uri(document)
    source = document.read_text(encoding="utf-8")
    original = source.splitlines()[line]
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
                    "text": source,
                }
            },
        )
        session.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [
                    {
                        "range": {
                            "start": {"line": line, "character": 0},
                            "end": {"line": line, "character": len(original)},
                        },
                        "text": replacement,
                    }
                ],
            },
        )
        result = session.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": line, "character": len(replacement)}},
        )
        return {item["label"] for item in result["items"]}
    finally:
        session.close()


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    assert_multiline_inline_tokens(root)
    declaration_document = root / "tests" / "required" / "declaration_shorthands" / "main.dl"
    declaration_tokens = tagged_tokens(root, declaration_document)
    expected_comments = {
        "<comment># replacement shorthand comment</comment>",
        "<comment># action shorthand comment</comment>",
        "<comment># value shorthand comment</comment>",
    }
    missing_comments = {comment for comment in expected_comments if comment not in declaration_tokens}
    if missing_comments:
        raise AssertionError(f"missing shorthand comment tokens: {sorted(missing_comments)!r}")

    mapping_document = root / "tests" / "required" / "declaration_shorthand_source_mapping" / "main.dl"
    mapping_tokens = tagged_tokens(root, mapping_document)
    expected_fragments = (
        "<keyword>to</keyword><section> </section><patternDefinition>announce </patternDefinition><variable>value</variable>",
        "<section>:</section> <function>print </function><variable>value</variable><function> as a line</function>",
        "<comment># mapped shorthand comment</comment>",
    )
    for fragment in expected_fragments:
        if fragment not in mapping_tokens:
            raise AssertionError(f"missing mapped shorthand token fragment: {fragment!r}\n{mapping_tokens}")
    if "value" not in completion_labels(root, mapping_document, 3, 29):
        raise AssertionError("completion hid an inline shorthand parameter from its transformed body")
    edited_labels = edited_completion_labels(root, mapping_document, 3, "    to announce value: r")
    if "return " not in edited_labels:
        raise AssertionError(f"transformed completion used a stale inline body: {sorted(edited_labels)!r}")


if __name__ == "__main__":
    main()
