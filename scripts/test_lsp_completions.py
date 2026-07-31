#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


EDIT_LINE = 3


def completion_items(
    session: LspSession,
    uri: str,
    prefix: str,
    edit_line: int = EDIT_LINE,
) -> list[dict]:
    result = session.request(
        "textDocument/completion",
        {
            "textDocument": {"uri": uri},
            "position": {"line": edit_line, "character": len(prefix)},
        },
    )
    return result["items"]


def completion_labels(
    session: LspSession,
    uri: str,
    prefix: str,
    edit_line: int = EDIT_LINE,
) -> set[str]:
    return {
        item["label"]
        for item in completion_items(session, uri, prefix, edit_line)
    }


def replace_line(
    session: LspSession,
    uri: str,
    version: int,
    previous: str,
    replacement: str,
    edit_line: int = EDIT_LINE,
) -> None:
    session.notify(
        "textDocument/didChange",
        {
            "textDocument": {"uri": uri, "version": version},
            "contentChanges": [
                {
                    "range": {
                        "start": {"line": edit_line, "character": 0},
                        "end": {"line": edit_line, "character": len(previous)},
                    },
                    "text": replacement,
                }
            ],
        },
    )


def test_substitution_completions(root: pathlib.Path) -> None:
    edit_line = 20
    document = root / "tests" / "lsp" / "completion_substitutions" / "main.dl"
    uri = to_file_uri(document)
    source = document.read_text()
    current_line = source.splitlines()[edit_line]
    version = 1
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": version,
                    "text": source,
                }
            },
        )
        session.notify(
            "dynlex/activeCursorChanged",
            {
                "clientId": "completion-substitution-regression",
                "uri": uri,
                "version": version,
                "position": {"line": edit_line, "character": len(current_line)},
            },
        )

        def edit(replacement: str) -> list[dict]:
            nonlocal current_line, version
            version += 1
            replace_line(
                session,
                uri,
                version,
                current_line,
                replacement,
                edit_line,
            )
            current_line = replacement
            return completion_items(session, uri, replacement, edit_line)

        empty_argument_items = edit("get ")
        empty_argument_labels = [item["label"] for item in empty_argument_items]
        for expected in ["zulu", "sheep", "wolf"]:
            if expected not in empty_argument_labels:
                raise AssertionError(
                    "completion omitted a function-pattern substitution from an "
                    f"empty argument slot: {empty_argument_labels!r}"
                )
        if empty_argument_labels.index("zulu") > min(
            empty_argument_labels.index("sheep"),
            empty_argument_labels.index("wolf"),
        ):
            raise AssertionError(
                "a nested substitution outranked the direct literal after 'get '"
            )
        empty_argument_items_by_label = {
            item["label"]: item for item in empty_argument_items
        }
        if empty_argument_items_by_label["zulu"]["sortText"] >= min(
            empty_argument_items_by_label["sheep"]["sortText"],
            empty_argument_items_by_label["wolf"]["sortText"],
        ):
            raise AssertionError(
                "completion sort keys do not prioritize a direct literal over "
                "nested substitutions"
            )

        partial_items = edit("get s")
        partial_labels = [item["label"] for item in partial_items]
        if "sheep" not in partial_labels:
            raise AssertionError(
                f"partial substitution did not complete sheep: {partial_labels!r}"
            )
        if " in" in partial_labels:
            raise AssertionError(
                "a typed unknown-argument path outranked the matching substitution"
            )

        completed_substitution_labels = {
            item["label"] for item in edit("get sheep ")
        }
        if "in" not in completed_substitution_labels:
            raise AssertionError(
                "completion did not resume the parent pattern after a substitution"
            )

        typed_unknown_labels = {item["label"] for item in edit("get unknown ")}
        if "in" not in typed_unknown_labels:
            raise AssertionError(
                "completion rejected an unknown argument name already typed by the user"
            )
    finally:
        session.close()


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    document = root / "tests" / "lsp" / "completions" / "main.dl"
    uri = to_file_uri(document)
    source = document.read_text()
    current_line = source.splitlines()[EDIT_LINE]
    version = 1
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        session.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "dynlex",
                    "version": version,
                    "text": source,
                }
            },
        )
        session.notify(
            "dynlex/activeCursorChanged",
            {
                "clientId": "completion-regression",
                "uri": uri,
                "version": version,
                "position": {"line": EDIT_LINE, "character": len(current_line)},
            },
        )

        def edit(replacement: str) -> set[str]:
            nonlocal current_line, version
            version += 1
            replace_line(session, uri, version, current_line, replacement)
            current_line = replacement
            return completion_labels(session, uri, replacement)

        get_prefix_labels = edit("g")
        if "get " not in get_prefix_labels or "get" in get_prefix_labels:
            raise AssertionError(
                "partial get completion did not include its deterministic trailing space: "
                f"{sorted(get_prefix_labels)!r}"
            )

        row_prefix_labels = edit("r")
        if "row " not in row_prefix_labels or "row" in row_prefix_labels:
            raise AssertionError(
                "partial row completion did not reach the shared branch point: "
                f"{sorted(row_prefix_labels)!r}"
            )

        row_labels = edit("row ")
        expected_row = {"across the ", "back", "to the other "}
        if not expected_row.issubset(row_labels):
            raise AssertionError(
                f"row completion omitted literal continuations: {sorted(row_labels)!r}"
            )
        invalid_row = {"*", "+", "-", "/", "= ", "<value>", "<expression>"}
        if invalid_row.intersection(row_labels):
            raise AssertionError(
                f"row completion leaked argument-first patterns: {sorted(row_labels)!r}"
            )

        unknown_labels = edit("x ")
        if not {"*", "= "}.issubset(unknown_labels):
            raise AssertionError(
                "a manually typed unknown identifier was not accepted as an argument: "
                f"{sorted(unknown_labels)!r}"
            )

        partial_labels = edit("set x t")
        if "to " not in partial_labels:
            raise AssertionError(
                f"partial literal completion did not follow the set pattern: {sorted(partial_labels)!r}"
            )

        introduced_labels = edit("set x to ")
        if "x" not in introduced_labels:
            raise AssertionError(
                "a typed identifier was not available later in the same sentence: "
                f"{sorted(introduced_labels)!r}"
            )
        if any(label.startswith("<") and label.endswith(">") for label in introduced_labels):
            raise AssertionError(
                f"completion invented placeholder names: {sorted(introduced_labels)!r}"
            )

        existing_labels = edit("set ")
        if "existing" not in existing_labels:
            raise AssertionError(
                f"completion omitted an existing in-scope variable: {sorted(existing_labels)!r}"
            )
        if "previous" in existing_labels:
            raise AssertionError(
                "completion suggested a name introduced only by the active line"
            )
        if any(label.startswith("<") and label.endswith(">") for label in existing_labels):
            raise AssertionError(
                f"completion invented a destination name: {sorted(existing_labels)!r}"
            )

        existing_prefix_items = completion_items(session, uri, "set ")
        if not any(item["label"] == "existing" for item in existing_prefix_items):
            raise AssertionError("existing variable completion item disappeared")

        root_prefix_labels = edit("exi")
        if "existing" not in root_prefix_labels:
            raise AssertionError(
                f"partial existing variable was not completed: {sorted(root_prefix_labels)!r}"
            )

        river_argument_labels = edit("get ")
        missing_passengers = {"hay", "sheep", "wolf"} - river_argument_labels
        if missing_passengers:
            raise AssertionError(
                "river passenger substitutions were omitted after 'get ': "
                f"{sorted(missing_passengers)!r}"
            )
    finally:
        session.close()

    test_substitution_completions(root)
    print("Completions follow literal specificity and only suggest known names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
