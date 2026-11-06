#!/usr/bin/env python3
"""Export the IDE and extension logos from web/icons/dynlex-icon.svg.

Requires PyGObject and GdkPixbuf with the SVG loader (librsvg).
"""

from pathlib import Path
from shutil import copyfile

import gi

gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf


def main():
    project = Path(__file__).resolve().parent.parent
    source = project / "web/icons/dynlex-icon.svg"

    for relative in (
        "src/web/ide/public/icons/dynlex-icon.svg",
        "vscode-extension/icons/dynlex.svg",
    ):
        target = project / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, target)
        print(f"Generated {relative}")

    for relative, size in (
        ("web/icons/dynlex-icon.png", 1024),
        ("vscode-extension/icons/dynlex.png", 256),
    ):
        image = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(source), size, size, True)
        image.savev(str(project / relative), "png", [], [])
        print(f"Generated {relative} ({size} x {size})")


if __name__ == "__main__":
    main()
