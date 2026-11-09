#!/usr/bin/env python3
"""Drive the real declarative window on a private X server, using X11 events."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from native_ui_test import click, close_window, named_window, save_png, screenshot, x11_library
from native_ui_test import isolated_x11_environment
from find_vulkan_icd import find_vulkan_icd, vulkan_driver_environment


def drive(project, executable, snapshot):
    x = x11_library()
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Could not connect to the private X server")
    application = subprocess.Popen([str(executable)], cwd=project, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        root = x.XDefaultRootWindow(display)
        deadline = time.monotonic() + 30
        window = None
        while window is None and time.monotonic() < deadline:
            if application.poll() is not None:
                raise RuntimeError(f"UI exited before opening: {application.communicate()}")
            window = named_window(x, display, root, b"project window")
            time.sleep(0.05)
        if window is None:
            raise RuntimeError("The declarative window did not open")
        # Resource creation precedes the first paint. Wait for the button fill.
        time.sleep(1)
        while time.monotonic() < deadline:
            image = screenshot(x, display, window, 840, 600)
            try:
                if x.XGetPixel(image, 40, 180) & 0xFFFFFF == 0x207B57:
                    break
            finally:
                x.XDestroyImage(image)
            time.sleep(0.1)
        else:
            raise RuntimeError("The declarative toolbar was not rendered")
        click(x, display, root, window, 100, 190)
        click(x, display, root, window, 100, 190)
        click(x, display, root, window, 210, 190)
        x.XWarpPointer(display, 0, window, 0, 0, 0, 0, 780, 480)
        x.XFlush(display)
        time.sleep(0.3)
        image = screenshot(x, display, window, 840, 600)
        try:
            if x.XGetPixel(image, 50, 248) & 0xFFFFFF != 0x395DBE:
                raise RuntimeError("The declarative blue table row was not rendered")
            if snapshot:
                save_png(image, snapshot)
        finally:
            x.XDestroyImage(image)
        close_window(x, display, window)
        output, errors = application.communicate(timeout=10)
        if application.returncode != 0 or output.strip() != "2\nDraw selected" or errors:
            raise RuntimeError(f"UI event dispatch failed: exit={application.returncode}, stdout={output!r}, stderr={errors!r}")
    finally:
        if application.poll() is None:
            application.terminate()
            application.wait(timeout=5)
        x.XCloseDisplay(display)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--drive", type=Path)
    parser.add_argument("--snapshot", type=Path)
    args = parser.parse_args()
    project, compiler = args.project.resolve(), args.compiler.resolve()
    if args.drive:
        drive(project, args.drive.resolve(), args.snapshot)
        return
    xvfb = shutil.which("xvfb-run")
    if xvfb is None:
        raise RuntimeError("The native UI test requires xvfb-run")
    with tempfile.TemporaryDirectory(prefix="dynlex-ui-sections-") as directory:
        executable = Path(directory) / "window.out"
        subprocess.run([str(compiler), str(project / "tests/runtime/ui_sections_window.dl"), "-o", str(executable)], cwd=project, check=True)
        command = [xvfb, "--auto-servernum", "--server-args=-screen 0 1100x760x24", sys.executable, str(Path(__file__).resolve()), str(project), str(compiler), "--drive", str(executable)]
        if args.snapshot:
            command += ["--snapshot", str(args.snapshot.resolve())]
        subprocess.run(command, cwd=project, env=isolated_x11_environment(vulkan_driver_environment(find_vulkan_icd(), os.environ)), check=True, timeout=60)


if __name__ == "__main__":
    main()
