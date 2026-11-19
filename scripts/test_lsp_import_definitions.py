#!/usr/bin/env python3
"""Definition navigation follows the compiler's actual import resolution."""
import pathlib
import tempfile

from lsp_tokens import LspSession, default_server_path, initialize_session


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory() as temporary:
        project = pathlib.Path(temporary).resolve()
        folder = project / "nested"
        folder.mkdir()
        local = folder / "café.dl"
        local.write_text("# sibling\n", encoding="utf-8")
        (project / local.name).write_text("# other file with the same name\n", encoding="utf-8")
        helper = folder / "helper.dl"
        helper.write_text("import café.dl\n", encoding="utf-8")
        entry = project / "main.dl"
        source = "import std.dl  # standard library\nimport nested/helper.dl\nimport nested/helper.dl\n"
        entry.write_text(source, encoding="utf-8")
        session = LspSession(default_server_path(root), project, False, False)
        try:
            initialize_session(session, project)
            session.notify("textDocument/didOpen", {"textDocument": {
                "uri": entry.as_uri(), "languageId": "dynlex", "version": 1, "text": source,
            }})

            def definition(file, line, column):
                return session.request("textDocument/definition", {
                    "textDocument": {"uri": file.as_uri()}, "position": {"line": line, "character": column},
                })

            start = {"line": 0, "character": 0}
            expected = {"start": start, "end": start}
            std = definition(entry, 0, 7)
            assert std and std["uri"].endswith("/lib/std.dl") and std["range"] == expected, std
            for line in (1, 2):
                assert definition(entry, line, 18) == {"uri": helper.as_uri(), "range": expected}
            assert definition(helper, 0, 11) == {"uri": local.as_uri(), "range": expected}
            assert definition(entry, 0, 15) is None, "The trailing comment resolved as an import."
            assert definition(entry, 0, 13) is None, "Whitespace after the path resolved as an import."
        finally:
            session.close()
    print("Import definitions open standard libraries and the selected project files.")


if __name__ == "__main__":
    main()
