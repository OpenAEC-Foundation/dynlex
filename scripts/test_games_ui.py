#!/usr/bin/env python3
"""Drive game windows on a private X display; never target the desktop session."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from find_vulkan_icd import find_vulkan_icd, vulkan_driver_environment
from native_ui_test import (
    click, close_window, image_region, isolated_x11_environment, named_window,
    press_key, save_png, screenshot, x11_library,
)
from play_game import GAMES, build_game


def drive(project, name, executable, snapshots):
    game = GAMES[name]
    x = x11_library()
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("Could not open the private test display")
    app = subprocess.Popen([str(executable)], cwd=project, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        root = x.XDefaultRootWindow(display)
        deadline = time.monotonic() + 30
        window = None
        while window is None and time.monotonic() < deadline:
            if app.poll() is not None:
                raise RuntimeError(f"{name} exited before opening: {app.communicate()}")
            window = named_window(x, display, root, game.window.encode())
            time.sleep(0.05)
        if window is None:
            raise RuntimeError(f"{name} did not open its window on the private display")
        time.sleep(1)

        def capture(label, width=game.width, height=game.height):
            image = screenshot(x, display, window, width, height)
            try:
                if snapshots:
                    save_png(image, snapshots / f"{name}-{label}.png")
                if width != game.width:
                    return b""
                if name == "typography":
                    return image_region(image, 64, 152, 640, 380)
                if name == "snake":
                    return image_region(image, 368, 168, 528, 528)
                return image_region(image, 380, 184, min(720, width - 380), min(340, height - 184))
            finally:
                x.XDestroyImage(image)

        initial = capture("opening")
        if len({initial[offset:offset + 4] for offset in range(0, len(initial), 4)}) < 2:
            raise RuntimeError(f"{name} did not paint its scene")
        if name == "geometry":
            def attempt_caption():
                image = screenshot(x, display, window, game.width, game.height)
                try:
                    return image_region(image, 28, 50, 140, 28)
                finally:
                    x.XDestroyImage(image)

            first_attempt = attempt_caption()
            time.sleep(0.2)
            if capture("ready-still") != initial:
                raise RuntimeError("Geometry Dash advanced behind its start screen")
            press_key(x, display, root, window, 0x20, interval=0.05)
            time.sleep(0.15)
            if capture("playing") == initial:
                raise RuntimeError("Space did not start Geometry Dash")
        if name not in ("snake", "typography"):
            key = ord("p") if name in ("geometry", "cube", "city") else 0x20
            press_key(x, display, root, window, key, interval=0.05)
            time.sleep(0.2)
            paused = capture("paused")
            time.sleep(0.25)
            if capture("paused-still") != paused:
                raise RuntimeError(f"{name}'s scene changed while paused")
            press_key(x, display, root, window, key, interval=0.05)
            if name in ("cube", "city"):
                press_key(x, display, root, window, ord("w"), interval=0.2)
            if name == "geometry":
                press_key(x, display, root, window, 0x20, interval=0.08)
            time.sleep(0.2)
            if capture("active") == paused:
                raise RuntimeError(f"{name}'s scene did not advance after resuming")
            press_key(x, display, root, window, ord("r"), interval=0.05)
            capture("reset")
            if name == "geometry":
                time.sleep(0.1)
                if attempt_caption() == first_attempt:
                    raise RuntimeError("Retry did not update the attempt caption")
                click(x, display, root, window, 1176, 44)
                time.sleep(0.1)
                frozen = capture("mouse-paused")
                time.sleep(0.2)
                if capture("mouse-paused-still") != frozen:
                    raise RuntimeError("The mouse Pause button did not freeze the level")
                click(x, display, root, window, 640, 398)
                time.sleep(0.15)
                if capture("mouse-resumed") == frozen:
                    raise RuntimeError("The mouse Resume button did not resume the level")
                press_key(x, display, root, window, ord("r"), interval=0.05)
                click(x, display, root, window, 800, 480)
                image = screenshot(x, display, window, game.width, game.height)
                try:
                    airborne = image_region(image, 210, 330, 100, 220)
                finally:
                    x.XDestroyImage(image)
                if not any(airborne[offset + 1] > 210 and airborne[offset] < 140 and airborne[offset + 2] < 230 for offset in range(0, len(airborne), 4)):
                    raise RuntimeError("Clicking the playfield did not make the cube jump")
                capture("mouse-jump")
        if name == "breakout":
            click(x, display, root, window, 272, 560)
            click(x, display, root, window, 253, 638)
            capture("violet")
        if name == "shaders":
            press_key(x, display, root, window, 0x20, interval=0.05)
            studies = []
            for index in range(5):
                click(x, display, root, window, 172, 312 + index * 56)
                deadline = time.monotonic() + 10
                rendered = capture(f"study-{index}")
                while studies and rendered == studies[-1] and time.monotonic() < deadline:
                    time.sleep(0.1)
                    rendered = capture(f"study-{index}")
                studies.append(rendered)
            if len(set(studies)) != 5:
                raise RuntimeError("The gallery buttons did not select five distinct studies")
        x.XResizeWindow(display, window, game.width * 3 // 4, game.height * 3 // 4)
        x.XFlush(display)
        time.sleep(0.25)
        capture("resized", game.width * 3 // 4, game.height * 3 // 4)
        close_window(x, display, window)
        output, errors = app.communicate(timeout=10)
        if app.returncode or output or errors:
            raise RuntimeError(f"{name}: exit={app.returncode}, stdout={output!r}, stderr={errors!r}")
        print(f"{name}: native UI checks passed")
    finally:
        if app.poll() is None:
            app.terminate()
            app.wait(timeout=5)
        x.XCloseDisplay(display)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", choices=GAMES, action="append")
    parser.add_argument("--snapshots", type=Path)
    parser.add_argument("--drive", type=Path)
    args = parser.parse_args()
    if args.drive and (args.game is None or len(args.game) != 1):
        parser.error("--drive requires exactly one --game")
    project = Path(__file__).resolve().parent.parent
    snapshots = args.snapshots.resolve() if args.snapshots else None
    if snapshots:
        snapshots.mkdir(parents=True, exist_ok=True)
    if args.drive:
        drive(project, args.game[0], args.drive.resolve(), snapshots)
        return
    xvfb = shutil.which("xvfb-run")
    if xvfb is None:
        raise RuntimeError("Native game verification requires xvfb-run")
    environment = isolated_x11_environment(vulkan_driver_environment(find_vulkan_icd(), os.environ))
    for name in args.game or GAMES:
        executable = build_game(project, name)
        command = [xvfb, "--auto-servernum", "--server-args=-screen 0 1600x1000x24", sys.executable, str(Path(__file__).resolve()), "--game", name, "--drive", str(executable)]
        if snapshots:
            command += ["--snapshots", str(snapshots)]
        subprocess.run(command, cwd=project, env=environment, check=True, timeout=90)


if __name__ == "__main__":
    main()
