#!/usr/bin/env python3
"""Serve the current web build for local development."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class WebHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, deployment=False, **kwargs):
        self.deployment = deployment
        super().__init__(*args, **kwargs)

    def send_header(self, keyword, value):
        super().send_header(keyword, value)
        if not self.deployment and keyword.lower() == "content-type" and value.split(";", 1)[0] == "text/html":
            # Evict files cached before this policy, including delayed imports
            # that a hard reload alone can still reuse. Keep storage and cookies.
            super().send_header("Clear-Site-Data", '"cache"')

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache" if self.deployment else "no-store")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", type=int, default=8000)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parent.parent / "web")
    parser.add_argument("--deployment", action="store_true", help="Use deployment cache headers for navigation tests")
    args = parser.parse_args()
    handler = partial(WebHandler, directory=str(args.directory), deployment=args.deployment)
    with ThreadingHTTPServer((args.bind, args.port), handler) as server:
        server.serve_forever()


if __name__ == "__main__":
    main()
