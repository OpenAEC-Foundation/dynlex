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


def pattern_frontiers(
    session: LspSession,
    uri: str,
    prefix: str,
    edit_line: int = EDIT_LINE,
) -> list[dict]:
    result = session.request(
        "dynlex/patternFrontier",
        {
            "textDocument": {"uri": uri},
            "position": {"line": edit_line, "character": len(prefix)},
        },
    )
    return result["frontiers"]


def frontier_transitions(frontiers: list[dict]) -> set[tuple[str, str]]:
    return {
        (transition["kind"], transition.get("text", ""))
        for frontier in frontiers
        for transition in frontier["transitions"]
    }


def accepted_continuations(
    session: LspSession,
    uri: str,
    continuations: list[str],
    prefix: str,
    edit_line: int = EDIT_LINE,
) -> set[int]:
    result = session.request(
        "dynlex/filterContinuations",
        {
            "textDocument": {"uri": uri},
            "position": {"line": edit_line, "character": len(prefix)},
            "continuations": continuations,
        },
    )
    return set(result["accepted"])


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


def test_numeric_pattern_frontier(root: pathlib.Path) -> None:
    document = root / "tests" / "lsp" / "pattern_frontier_numeric" / "main.dl"
    uri = to_file_uri(document)
    source = document.read_text()
    line_index = 4
    line = source.splitlines()[line_index]
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
        frontiers = pattern_frontiers(session, uri, line, line_index)
        if not any(frontier["canComplete"] for frontier in frontiers):
            raise AssertionError(
                "a complete pattern ending in a numeric argument was not reported "
                f"as complete: {frontiers!r}"
            )

        prefix = "set x to"
        continuations = [
            " 1\n",
            " 1\nset y to ",
            " 1 junk\n",
            list(b" nonsense words\n"),
        ]
        accepted = accepted_continuations(
            session, uri, continuations, prefix, line_index
        )
        expected_accepted = {0, 1}
        if accepted != expected_accepted:
            raise AssertionError(
                "batch continuation filtering did not validate complete and partial "
                f"lines: expected {sorted(expected_accepted)!r}, got {sorted(accepted)!r}"
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

        row_frontiers = pattern_frontiers(session, uri, "row ")
        row_transitions = frontier_transitions(row_frontiers)
        expected_row_transitions = {
            ("literal", "across"),
            ("literal", "back"),
            ("literal", "to"),
        }
        if not expected_row_transitions.issubset(row_transitions):
            raise AssertionError(
                "pattern frontier omitted viable literal pattern-tree edges: "
                f"{sorted(row_transitions)!r}"
            )
        if any(frontier["canComplete"] for frontier in row_frontiers):
            raise AssertionError("incomplete 'row ' pattern was reported as complete")

        edit("row a")
        partial_row_transitions = frontier_transitions(pattern_frontiers(session, uri, "row a"))
        if ("literal", "cross") not in partial_row_transitions:
            raise AssertionError(
                "pattern frontier did not consume the generated literal prefix: "
                f"{sorted(partial_row_transitions)!r}"
            )
        if ("literal", "across") in partial_row_transitions:
            raise AssertionError(
                "pattern frontier returned an already partially consumed literal"
            )
        if ("literal", "") in partial_row_transitions:
            raise AssertionError("pattern frontier returned an empty literal transition")

        edit("row")
        continuations = [
            " across",
            " back",
            " sideways",
            " acrossx\n",
        ]
        accepted = accepted_continuations(session, uri, continuations, "row")
        expected_accepted = {0, 1, 2}
        if accepted != expected_accepted:
            raise AssertionError(
                "batch continuation filtering did not follow recursive matcher paths "
                f"across token and line boundaries: expected {sorted(expected_accepted)!r}, "
                f"got {sorted(accepted)!r}"
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

        introduced_frontiers = pattern_frontiers(session, uri, "set x to ")
        if ("argument", "") not in frontier_transitions(introduced_frontiers):
            raise AssertionError(
                "pattern frontier did not expose the recursive argument edge"
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
    test_numeric_pattern_frontier(root)
    print("Completions follow literal specificity and only suggest known names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
