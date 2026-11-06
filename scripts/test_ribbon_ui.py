#!/usr/bin/env python3
"""Check the real ribbon demo on a private display, without touching the desktop."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from find_vulkan_icd import find_vulkan_icd, vulkan_driver_environment
from native_ui_test import click, close_window, image_region, named_window, press_key, save_png, screenshot, window_geometry, window_size_hints, x11_library


TABS = ["File", "Home", "Insert", "Design", "Transitions", "Animations", "Slide Show", "Review", "View"]
CENTERS = [53, 115, 184, 260, 353, 461, 565, 655, 724]
CHROME = [(218, 14, 32, 30), (256, 14, 32, 30), (294, 14, 32, 30),
          (360, 13, 428, 32), (1000, 14, 106, 30), (1116, 14, 92, 30),
          (1225, 14, 30, 30), (997, 779, 24, 21), (1030, 779, 24, 21)]
HOME = [(28, 105, 68, 84), (102, 105, 92, 28), (102, 136, 92, 28),
        (102, 167, 136, 28), (262, 105, 79, 84), (347, 105, 110, 28),
        (347, 136, 92, 28), (347, 167, 110, 28), (481, 105, 142, 28),
        (627, 105, 48, 28), (481, 136, 30, 28), (515, 136, 30, 28),
        (549, 136, 30, 28), (583, 136, 30, 28), (617, 136, 30, 28),
        (699, 105, 30, 28), (733, 105, 30, 28), (767, 105, 30, 28),
        (699, 136, 140, 28), (699, 167, 154, 28), (877, 105, 68, 84),
        (951, 105, 68, 84), (1043, 105, 92, 28), (1043, 136, 92, 28),
        (1043, 167, 110, 28)]


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
        if window_size_hints(x, display, window).flags & (1 << 7):
            raise RuntimeError("The ribbon locks the native window's aspect ratio")
        time.sleep(1)
        if app.poll() is not None:
            raise RuntimeError(f"The ribbon failed after opening: {app.communicate()}")
        document = None

        def move(horizontal, vertical):
            x.XWarpPointer(display, 0, window, 0, 0, 0, 0, horizontal, vertical)
            x.XFlush(display)
            time.sleep(0.25)

        def region(box, name=None):
            if app.poll() is not None:
                raise RuntimeError(f"The ribbon exited: {app.communicate()}")
            _, _, width, height = window_geometry(x, display, window)
            image = screenshot(x, display, window, width, height)
            try:
                if snapshots and name:
                    save_png(image, snapshots / f"ribbon-{name}.png")
                return image_region(image, *box)
            finally:
                x.XDestroyImage(image)

        def hover(box):
            move(20, 250)
            before = region(box)
            left, top, width, height = box
            move(left + width // 2, top + height // 2)
            await_region(box, lambda current: current != before,
                         f"A visible control has no hover feedback: {box}")

        def await_region(box, predicate, message):
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                current = region(box)
                if predicate(current):
                    return current
                time.sleep(0.05)
            region(box, 'failure')
            raise RuntimeError(message)

        def open_menu(horizontal, vertical, box, closed):
            click(x, display, root, window, horizontal, vertical)
            await_region(box, lambda current: current != closed,
                         'The menu did not open before its next interaction')

        def capture(tab, name=None, width=1280, height=800):
            nonlocal document
            await_region((CENTERS[tab], 92, 1, 1), lambda current: current[:3] == b'\x26\x41\xb8',
                         f'The {TABS[tab]} tab was not selected')
            time.sleep(0.25)
            image = screenshot(x, display, window, width, height)
            try:
                pixel = x.XGetPixel(image, CENTERS[tab], 92) & 0xFFFFFF
                if pixel != 0xB84126:
                    raise RuntimeError(f"The {TABS[tab]} tab is not selected: indicator={pixel:06x}")
                if (width, height) == (1280, 800):
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
        for box in CHROME + HOME:
            hover(box)
        move(20, 250)
        # Menu content overlays the workspace and captures clicks and Escape.
        popup = (28, 193, 196, 140)
        closed = region(popup)
        open_menu(62, 146, popup, closed)
        region(popup, 'paste-menu')
        press_key(x, display, root, window, 0xFF1B)
        await_region(popup, lambda current: current == closed,
                     "Escape did not dismiss the popup without closing the app")
        open_menu(62, 146, popup, closed)
        click(x, display, root, window, CENTERS[2], 77)  # First outside click is captured.
        await_region(popup, lambda current: current == closed,
                     'Clicking outside did not dismiss the popup')
        capture(1)
        open_menu(62, 146, popup, closed)
        click(x, display, root, window, 90, 247)
        await_region(popup, lambda current: current == closed,
                     "Selecting a menu item did not close the popup")
        # Fields retain selected text; arrows and Enter work inside the menu.
        field = (489, 109, 112, 18)  # Label interior; exclude focus border and dropdown arrow.
        before = region(field)
        field_popup = (481, 137, 196, 140)
        open_menu(550, 119, field_popup, region(field_popup))
        press_key(x, display, root, window, 0xFF54)
        press_key(x, display, root, window, 0xFF54)
        press_key(x, display, root, window, 0xFF0D)
        move(20, 250)
        click(x, display, root, window, 20, 250)  # Compare text after explicitly leaving keyboard focus.
        time.sleep(0.4)  # Let the field's hover fade before comparing retained text.
        selected = region(field, "font-selected")
        if max(abs(a-b) for a, b in zip(selected, before)) <= 64:
            raise RuntimeError("Selecting a font did not update the field")
        click(x, display, root, window, CENTERS[2], 77)
        click(x, display, root, window, CENTERS[1], 77)
        move(20, 250)
        restored = region(field, "font-restored")
        # Quantized hover shading can differ slightly while the text stays fixed.
        if max(abs(a-b) for a, b in zip(restored, selected)) > 3:
            raise RuntimeError("Switching tabs changed the field's rendered label")
        # A regular command has no misleading dropdown caret or popup.
        click(x, display, root, window, 1060, 150)
        capture(1)
        click(x, display, root, window, CENTERS[0], 77)
        press_key(x, display, root, window, 0xFF09)
        move(20, 250)  # Pointer movement must preserve the focused ribbon tab.
        press_key(x, display, root, window, 0xFF51)  # The first focused header wraps to View.
        capture(8)
        press_key(x, display, root, window, 0xFF53)
        capture(0)
        press_key(x, display, root, window, 0xFF53)
        capture(1)
        click(x, display, root, window, 137, 119)  # An unbound Cut command is valid.
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
        # Guarantee that all six taps arrive between frames, independently of load.
        os.kill(app.pid, signal.SIGSTOP)
        try:
            for _ in range(6):
                press_key(x, display, root, window, 0xFF53, interval=0.06)
        finally:
            os.kill(app.pid, signal.SIGCONT)
        capture(5)
        def resize(left, top, width, height):
            x.XMoveResizeWindow(display, window, left, top, width, height)
            x.XFlush(display)
            time.sleep(0.5)
            actual = window_geometry(x, display, window)
            if actual != (left, top, width, height):
                raise RuntimeError(f'Resizing changed the requested window geometry: {actual}')
            if window_size_hints(x, display, window).flags & (1 << 7):
                raise RuntimeError('Resizing restored an aspect-ratio constraint')
            image = screenshot(x, display, window, width, height)
            try:
                if x.XGetPixel(image, width // 2, height - 22) & 0xFFFFFF != 0xDFE2E7:
                    raise RuntimeError('The status bar did not follow the bottom edge')
                if x.XGetPixel(image, width - 16, 120) & 0xFFFFFF != 0xFFFFFF:
                    raise RuntimeError('The ribbon did not fill the available width')
                if x.XGetPixel(image, 500, 250) & 0xFFFFFF != 0xEDEFF2:
                    raise RuntimeError('The ribbon wrapped and displaced the workspace')
            finally:
                x.XDestroyImage(image)

        # Change one dimension at a time, then mimic left/top edge resizing by
        # holding the opposite edge still. No real desktop is touched.
        move(20, 250)
        save_button = region(CHROME[0])
        resize(140, 160, 1600, 800)
        if region(CHROME[0]) != save_button:
            raise RuntimeError('A width-only resize magnified or moved the toolbar')
        press_key(x, display, root, window, 0xFF53)
        capture(6, 'wider', 1600, 800)  # Resizing preserves keyboard focus.
        resize(140, 160, 1280, 1000)
        capture(6, 'taller', 1280, 1000)
        resize(60, 160, 1360, 1000)  # Right edge stays at 1420.
        resize(60, 100, 1360, 1060)  # Bottom edge stays at 1160.
        resize(60, 100, 960, 600)
        capture(6, 'resized', 960, 600)
        click(x, display, root, window, CENTERS[1], 77)
        narrow_field = (field[0] + 24, *field[1:])
        await_region(narrow_field, lambda current: max(abs(a-b) for a, b in zip(current, selected)) <= 3,
                     'Resizing lost the selected font')
        hover((796, 14, 92, 30))  # Share follows the right edge at 960 pixels.
        click(x, display, root, window, CENTERS[3], 77)
        move(20, 250)
        strip = (42, 105, 876, 112)
        start = region(strip, 'scroll-start')
        earlier_arrow = (16, 120, 24, 80)
        later_arrow = (920, 120, 24, 80)

        def no_arrow(current):
            white = b'\xff' * (len(current) // 4)
            return all(current[channel::4] == white for channel in range(3))

        await_region(earlier_arrow, no_arrow,
                     'The previous-group arrow remained visible at the start')
        hidden_popup = (740, 193, 212, 120)
        outside = region(hidden_popup)
        click(x, display, root, window, 955, 150)  # The hidden Background control occupies this unclipped coordinate.
        if region(hidden_popup) != outside:
            raise RuntimeError('A clipped control accepted a click outside the ribbon viewport')
        hover((920, 104, 24, 108))
        click(x, display, root, window, 932, 156)
        end = await_region(strip, lambda current: current != start,
                           'The next-group arrow did not move the ribbon')
        time.sleep(0.5)
        end = region(strip, 'scroll-end')
        await_region(later_arrow, no_arrow,
                     'The next-group arrow remained visible at the end')
        hover(earlier_arrow)
        below_strip = (744, 230, 196, 76)
        closed = region(below_strip)
        click(x, display, root, window, 787, 146)  # Background, revealed by scrolling.
        await_region(below_strip, lambda current: current != closed,
                     'A scrolled dropdown was clipped to the ribbon strip')
        press_key(x, display, root, window, 0xFF1B)
        await_region(below_strip, lambda current: current == closed,
                     'Escape did not dismiss the scrolled dropdown')
        move(20, 250)
        time.sleep(0.4)
        end = region(strip)
        click(x, display, root, window, CENTERS[1], 77)
        click(x, display, root, window, CENTERS[3], 77)
        await_region(strip, lambda current: current == end,
                     'Switching tabs lost the strip position')
        await_region(later_arrow, no_arrow,
                     'Returning to a tab restored its hidden next-group arrow')
        click(x, display, root, window, 28, 156)
        await_region(strip, lambda current: current == start,
                     'The previous-group arrow did not restore the first group')
        await_region(earlier_arrow, no_arrow,
                     'The previous-group arrow remained visible after returning to the start')
        # Tab skips the hidden previous arrow and focuses the visible next arrow.
        press_key(x, display, root, window, 0xFF09)
        press_key(x, display, root, window, 0xFF0D)
        await_region(strip, lambda current: current != start,
                     'The focused next-group arrow did not respond to Enter')
        await_region(later_arrow, no_arrow,
                     'A keyboard-activated arrow retained its drawing or focus ring at the end')
        region((0, 0, 960, 600), 'scrolled')
        resize(60, 100, 2400, 1200)
        click(x, display, root, window, CENTERS[2], 77)
        capture(2, 'wide', 2400, 1200)
        image = screenshot(x, display, window, 2400, 1200)
        try:
            if snapshots:
                save_png(image, snapshots / 'ribbon-wide.png')
            # The slide scales inside the editor while chrome stays at native size.
            scale = min((2400-352)/928, (1200-168-168)/464)
            center_x = 290 + ((2400-352)-928*scale)/2 + 773*scale
            center_y = 264 + ((1200-168-168)-464*scale)/2 + 138*scale
            radius = 60*scale
            orange = []
            for py in range(int(center_y-radius)-2, int(center_y+radius)+3):
                for px in range(int(center_x-radius)-2, int(center_x+radius)+3):
                    if x.XGetPixel(image, px, py) & 0xFFFFFF == 0xCD6D47:
                        orange.append((px, py))
            if not orange:
                raise RuntimeError('The slide circle is missing after resizing')
            width = max(p[0] for p in orange) - min(p[0] for p in orange)
            height = max(p[1] for p in orange) - min(p[1] for p in orange)
            if abs(width-height) > 2:
                raise RuntimeError(f'The resized slide distorted a circle: {width} × {height}')
        finally:
            x.XDestroyImage(image)
        close_window(x, display, window)
        output, errors = app.communicate(timeout=10)
        if app.returncode or output or errors:
            raise RuntimeError(f"The ribbon failed: exit={app.returncode}, output={output!r}, errors={errors!r}")
        print("Ribbon UI: SVG icons, hover controls, menus, selection, nine tabs, independent resizing and clipped group scrolling passed")
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
        command = [xvfb, "--auto-servernum", "--server-args=-screen 0 2560x1600x24", sys.executable, str(Path(__file__).resolve()), str(project), str(compiler), "--drive", str(executable)]
        if snapshots:
            command += ["--snapshots", str(snapshots)]
        subprocess.run(command, cwd=project, env=vulkan_driver_environment(find_vulkan_icd(), os.environ), check=True, timeout=120)


if __name__ == "__main__":
    main()
