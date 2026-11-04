#!/usr/bin/env python3
"""Check the real ribbon demo on a private display, without touching the desktop."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from find_vulkan_icd import find_vulkan_icd, vulkan_driver_environment
from native_ui_test import click, close_window, image_region, named_window, press_key, save_png, screenshot, x11_library


TABS = ["File", "Home", "Insert", "Design", "Transitions", "Animations", "Slide Show", "Review", "View"]
CENTERS = [53, 115, 184, 260, 353, 461, 565, 655, 724]


def drive(project, executable, snapshots):
    x = x11_library()
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Could not connect to the private X server")
    app = subprocess.Popen([str(executable)], cwd=project, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        root = x.XDefaultRootWindow(display)
        deadline = time.monotonic() + 30
        window = None
        while window is None and time.monotonic() < deadline:
            if app.poll() is not None:
                raise RuntimeError(f"The ribbon exited before opening: {app.communicate()}")
            window = named_window(x, display, root, b"ribbon studio")
            time.sleep(0.05)
        if window is None:
            raise RuntimeError("Ribbon Studio did not open")
        time.sleep(1)
        document = None

        def capture(tab, name=None, scale=1.0):
            nonlocal document
            time.sleep(0.25)
            image = screenshot(x, display, window, int(1280 * scale), int(800 * scale))
            try:
                pixel = x.XGetPixel(image, int(CENTERS[tab] * scale), int(92 * scale)) & 0xFFFFFF
                if pixel != 0xB84126:
                    raise RuntimeError(f"The {TABS[tab]} tab is not selected: indicator={pixel:06x}")
                if scale == 1.0:
                    content = image_region(image, 20, 105, 1240, 112)
                    current = image_region(image, 240, 242, 1000, 530)
                    if document is None:
                        document = current
                    elif document != current:
                        raise RuntimeError("Switching ribbon tabs moved or changed the document workspace")
                else:
                    content = b""
                if snapshots and name:
                    save_png(image, snapshots / f"ribbon-{name}.png")
                return content
            finally:
                x.XDestroyImage(image)

        capture(1, "home")
        press_key(x, display, root, window, 0xFF09)
        press_key(x, display, root, window, 0xFF51)  # The first focused header wraps to View.
        capture(8)
        press_key(x, display, root, window, 0xFF53)
        capture(0)
        press_key(x, display, root, window, 0xFF53)
        capture(1)
        click(x, display, root, window, 61, 142)  # Unbound Paste is a valid command.
        capture(1)
        contents = set()
        for index, name in enumerate(TABS):
            click(x, display, root, window, CENTERS[index], 77)
            contents.add(capture(index, name.lower().replace(" ", "-")))
        if len(contents) != len(TABS):
            raise RuntimeError("Two ribbon tabs unexpectedly render the same commands")
        click(x, display, root, window, CENTERS[1], 77)
        press_key(x, display, root, window, 0xFF09)  # Tab focuses the selected header.
        press_key(x, display, root, window, 0xFF53)  # Right selects Insert.
        capture(2)
        press_key(x, display, root, window, 0xFF51)
        capture(1)
        press_key(x, display, root, window, 0xFF51)
        capture(0)
        press_key(x, display, root, window, 0xFF51)  # Wrap from File to View.
        capture(8)
        press_key(x, display, root, window, 0xFF0D)
        capture(8)
        # A run of short taps must keep both selection and the animated indicator coherent.
        for _ in range(6):
            press_key(x, display, root, window, 0xFF53, interval=0.06)
        capture(5)
        x.XResizeWindow(display, window, 960, 600)
        x.XFlush(display)
        time.sleep(0.5)
        click(x, display, root, window, int(CENTERS[3] * 0.75), 58)
        capture(3, "resized", 0.75)
        close_window(x, display, window)
        output, errors = app.communicate(timeout=10)
        if app.returncode or output or errors:
            raise RuntimeError(f"The ribbon failed: exit={app.returncode}, output={output!r}, errors={errors!r}")
        print("Ribbon UI: nine tabs, unbound commands, keyboard navigation, rapid switching and resizing passed")
    finally:
        if app.poll() is None:
            app.terminate()
            app.wait(timeout=5)
        x.XCloseDisplay(display)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--drive", type=Path)
    parser.add_argument("--snapshots", type=Path)
    args = parser.parse_args()
    project, compiler = args.project.resolve(), args.compiler.resolve()
    snapshots = args.snapshots.resolve() if args.snapshots else None
    if snapshots:
        snapshots.mkdir(parents=True, exist_ok=True)
    if args.drive:
        drive(project, args.drive.resolve(), snapshots)
        return
    xvfb = shutil.which("xvfb-run")
    if xvfb is None:
        raise RuntimeError("The ribbon test requires xvfb-run")
    with tempfile.TemporaryDirectory(prefix="dynlex-ribbon-ui-") as directory:
        executable = Path(directory) / "ribbon.out"
        result = subprocess.run([str(compiler), "examples/ribbon/main.dl", "-O2", "-o", str(executable)], cwd=project, text=True, capture_output=True)
        if result.returncode or result.stdout or result.stderr:
            raise RuntimeError(f"The ribbon must compile without diagnostics: {result.stdout}{result.stderr}")
        command = [xvfb, "--auto-servernum", "--server-args=-screen 0 1400x1000x24", sys.executable, str(Path(__file__).resolve()), str(project), str(compiler), "--drive", str(executable)]
        if snapshots:
            command += ["--snapshots", str(snapshots)]
        subprocess.run(command, cwd=project, env=vulkan_driver_environment(find_vulkan_icd(), os.environ), check=True, timeout=60)


if __name__ == "__main__":
    main()
