#!/usr/bin/env python3
import pathlib
import socket
import subprocess

from lsp_tokens import default_server_path


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    compiler = default_server_path(root)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        result = subprocess.run(
            [str(compiler), "--lsp", "--port", str(port)],
            cwd=root,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

    if result.returncode == 0:
        raise RuntimeError("TCP language server reported success after failing to bind")
    if "Failed to setup TCP server" not in result.stderr:
        raise RuntimeError(f"missing TCP setup diagnostic: {result.stderr!r}")

    print("TCP language server returns failure when its port cannot be bound")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
