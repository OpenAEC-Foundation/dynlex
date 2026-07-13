#!/usr/bin/env python3
import pathlib

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def progress(message: str) -> None:
    print(message, flush=True)


def open_document(session: LspSession, path: pathlib.Path) -> None:
    session.notify(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": to_file_uri(path),
                "languageId": "dynlex",
                "version": 1,
                "text": path.read_text(),
            }
        },
    )


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    server = default_server_path(root)
    documents = [
        root / "tests" / "games" / "geometry_dash" / "debug_sim.dl",
        root / "tests" / "games" / "snake.dl",
    ]
    progress("Starting concurrent stdio language servers")
    sessions = [LspSession(server, root, False, False) for _ in documents]
    try:
        if sessions[0].process.pid == sessions[1].process.pid:
            raise RuntimeError("concurrent LSP sessions unexpectedly share a process")

        for session, document in zip(sessions, documents):
            progress(f"Initializing session for {document.name}")
            initialize_session(session, root)
            progress(f"Opening {document.name}")
            open_document(session, document)

        for session, document in zip(sessions, documents):
            progress(f"Requesting semantic tokens for {document.name}")
            result = session.request(
                "textDocument/semanticTokens/full",
                {"textDocument": {"uri": to_file_uri(document)}},
            )
            progress(f"Received semantic tokens for {document.name}")
            data = result.get("data", [])
            if not data or len(data) % 5 != 0:
                raise RuntimeError(f"invalid semantic token response for {document}: {result}")
    finally:
        for session, document in zip(sessions, documents):
            progress(f"Closing session for {document.name}")
            session.close()
            progress(f"Closed session for {document.name}")

    print("Two concurrent stdio LSP sessions returned semantic tokens successfully")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
