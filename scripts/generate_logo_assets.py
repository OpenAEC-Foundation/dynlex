#!/usr/bin/env python3
"""Export extension logos from the compiler's canonical SVG.

Requires PyGObject and GdkPixbuf with the SVG loader (librsvg).
"""

from pathlib import Path
from shutil import copyfile

import gi

gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf


def main():
    project = Path(__file__).resolve().parent.parent
    source = project / "resources/branding/dynlex-icon.svg"

    for relative in (
        "vscode-extension/icons/dynlex.svg",
    ):
        target = project / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, target)
        print(f"Generated {relative}")

    for relative, size in (
        ("vscode-extension/icons/dynlex.png", 256),
    ):
        image = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(source), size, size, True)
        image.savev(str(project / relative), "png", [], [])
        print(f"Generated {relative} ({size} x {size})")


if __name__ == "__main__":
    main()
