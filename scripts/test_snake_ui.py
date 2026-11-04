#!/usr/bin/env python3
"""Exercise Snake Studio through its real native UI on an isolated X display."""
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
from native_ui_test import click, image_region, named_window, press_key, save_png, screenshot, x11_library


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
                raise RuntimeError(f"Snake exited before opening: {app.communicate()}")
            window = named_window(x, display, root, b"snake studio")
            time.sleep(0.05)
        if window is None:
            raise RuntimeError("Snake Studio did not open")
        time.sleep(1)

        def capture(name, width=1120, height=760):
            image = screenshot(x, display, window, width, height)
            try:
                if snapshots:
                    save_png(image, snapshots / f"snake-{name}.png")
                return image_region(image, 368, 168, 528, 528) if width == 1120 else b""
            finally:
                x.XDestroyImage(image)

        def mouse(horizontal, vertical):
            click(x, display, root, window, horizontal, vertical)

        def key(keysym, interval=0.2):
            press_key(x, display, root, window, keysym, interval=interval)

        capture("menu")
        # Tab/Enter navigation opens Settings without touching the mouse.
        key(0xFF09)
        key(0xFF09)
        key(0xFF0D)
        capture("settings-default")
        mouse(256, 258)  # Swift
        mouse(274, 316)  # Wrap edges
        mouse(274, 372)  # Hide grid
        mouse(274, 428)  # Reduce motion
        mouse(256, 506)  # Violet
        capture("settings")

        # Resizing changes both projection and pointer coordinates.
        x.XResizeWindow(display, window, 840, 570)
        x.XFlush(display)
        time.sleep(0.5)
        capture("resized", 840, 570)
        mouse(132, 465)  # Back, in the resized coordinate system
        x.XResizeWindow(display, window, 1120, 760)
        x.XFlush(display)
        time.sleep(0.5)
        key(0xFF0D)  # The default action on the menu starts a run.
        time.sleep(0.3)
        key(0x20, interval=0)  # A tap fully released before the next frame must still pause.
        time.sleep(0.15)
        still = capture("paused")
        time.sleep(0.35)
        if capture("paused-still") != still:
            raise RuntimeError("The playfield moved while paused with reduced motion enabled")
        mouse(176, 564)  # Preferences, preserving the paused run
        capture("preferences")
        key(0xFF1B)  # Back to pause
        key(0xFF1B)  # Resume
        key(0xFF54)  # Turn south
        time.sleep(0.2)
        if capture("playing") == still:
            raise RuntimeError("The playfield did not move after resuming")
        mouse(176, 488)  # End run
        capture("finished")
        mouse(176, 442)  # Try again
        time.sleep(0.3)
        mouse(176, 488)  # End the next run
        mouse(176, 498)  # Main menu
        press_key(x, display, root, window, 0xFF1B, release=False)  # The window closes on key press.
        output, errors = app.communicate(timeout=10)
        expected = "menu\n0\n2\ntrue\nfalse\ntrue\n2\ntrue\n"
        if app.returncode != 0 or output != expected or errors:
            raise RuntimeError(f"Snake UI failed: exit={app.returncode}, stdout={output!r}, stderr={errors!r}")
        print("Snake UI: mouse, keyboard, settings, pause, restart and resizing passed")
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
        raise RuntimeError("The native Snake test requires xvfb-run")
    with tempfile.TemporaryDirectory(prefix="dynlex-snake-ui-") as directory:
        executable = Path(directory) / "snake.out"
        def compile_fixture(source, output):
            result = subprocess.run([str(compiler), source, "-O2", "-o", str(output)], cwd=project, text=True, capture_output=True)
            if result.returncode != 0 or result.stdout or result.stderr:
                raise RuntimeError(f"Snake must compile without diagnostics: {result.stdout}{result.stderr}")

        compile_fixture("tests/runtime/snake_studio.dl", executable)
        command = [xvfb, "--auto-servernum", "--server-args=-screen 0 1400x1000x24", sys.executable, str(Path(__file__).resolve()), str(project), str(compiler), "--drive", str(executable)]
        if snapshots:
            command += ["--snapshots", str(snapshots)]
        environment = vulkan_driver_environment(find_vulkan_icd(), os.environ)
        subprocess.run(command, cwd=project, env=environment, check=True, timeout=60)
        rendering = Path(directory) / "rendering.out"
        compile_fixture("tests/runtime/snake_rendering.dl", rendering)
        result = subprocess.run([xvfb, "--auto-servernum", "--server-args=-screen 0 1400x1000x24", str(rendering)], cwd=project, env=environment, capture_output=True, text=True, timeout=60)
        rows = result.stdout.splitlines()
        if result.returncode or result.stderr or len(rows) != 2 or rows[-1] != "Snake rendering passed":
            raise RuntimeError(f"Snake rendering failed: exit={result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}")
        rate = float(rows[0])
        if rate <= 0:
            raise RuntimeError(f"The renderer reported an invalid frame rate: {rate}")
        print(f"Snake rendering: {rate:.1f} fps; full board and impact sequence passed")


if __name__ == "__main__":
    main()
