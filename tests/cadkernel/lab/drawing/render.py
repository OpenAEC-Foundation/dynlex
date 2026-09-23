#!/usr/bin/env python3
"""Render the two native circular-rim operations from Dynedra Lab output."""
from __future__ import annotations

import argparse
import hashlib
import html
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tests/cadkernel"))
from verify import run_process


EXPECTED = {
    6: ("Rim fillet", (3, 5, 4, 10, 4)),
    7: ("Rim chamfer", (3, 5, 4, 10, 4)),
}


def parse_output(output: str):
    shapes = {kind: {"edges": [], "guides": [], "points": []} for kind in EXPECTED}
    for raw in output.splitlines():
        fields = raw.strip().split(";")
        if not fields or fields[0] not in {"S", "E", "G", "P"}:
            continue
        kind = int(fields[1])
        if kind not in shapes:
            raise AssertionError(f"unexpected solid kind {kind}")
        shape = shapes[kind]
        if fields[0] == "S":
            if len(fields) != 9:
                raise AssertionError(f"invalid header: {raw}")
            shape["topology"] = tuple(map(int, fields[2:7]))
            shape["flaws"] = int(fields[7])
            shape["gap"] = float(fields[8])
        elif fields[0] == "P":
            shape["points"].append(tuple(map(float, fields[2:5])))
        else:
            values = tuple(map(float, fields[2:8]))
            target = "edges" if fields[0] == "E" else "guides"
            shape[target].append((values[:3], values[3:]))
    for kind, (_, topology) in EXPECTED.items():
        shape = shapes[kind]
        if shape.get("topology") != topology:
            raise AssertionError(f"solid {kind} topology {shape.get('topology')} != {topology}")
        if shape.get("flaws") != 0 or shape.get("gap", math.inf) > 1e-9:
            raise AssertionError(f"solid {kind} is invalid: {shape}")
        if not shape["edges"]:
            raise AssertionError(f"solid {kind} exported no edge segments")
    return shapes


def projected(point):
    yaw, pitch = 0.4, 0.7
    horizontal = math.cos(yaw) * point[0] - math.sin(yaw) * point[1]
    depth = math.sin(yaw) * point[0] + math.cos(yaw) * point[1]
    vertical = math.cos(pitch) * point[2] - math.sin(pitch) * depth
    return horizontal, vertical


def panel_geometry(shape, left, top, width, height):
    all_points = [point for segment in shape["edges"] for point in segment]
    all_points.extend(shape["points"])
    projected_points = [projected(point) for point in all_points]
    low_x = min(point[0] for point in projected_points)
    high_x = max(point[0] for point in projected_points)
    low_y = min(point[1] for point in projected_points)
    high_y = max(point[1] for point in projected_points)
    span_x = max(high_x - low_x, 1e-12)
    span_y = max(high_y - low_y, 1e-12)
    scale = min(width / span_x, height / span_y)
    center_x = (low_x + high_x) * 0.5
    center_y = (low_y + high_y) * 0.5

    def screen(point):
        x, y = projected(point)
        return left + width * 0.5 + (x - center_x) * scale, top + height * 0.5 - (y - center_y) * scale

    return screen


def svg_document(shapes, compiler_hash: str):
    width, height = 1200, 720
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<metadata>DynLex compiler SHA256 {html.escape(compiler_hash)}; native O2 geometry</metadata>',
        '<rect width="1200" height="720" fill="#f7f9fc"/>',
        '<text x="54" y="58" font-family="Arial, sans-serif" font-size="30" font-weight="700" fill="#17233c">Dynedra Lab</text>',
        '<text x="54" y="88" font-family="Arial, sans-serif" font-size="15" fill="#60708d">Native circular rim operations · exact B-rep edge sampling</text>',
    ]
    for column, kind in enumerate((6, 7)):
        name, _ = EXPECTED[kind]
        shape = shapes[kind]
        panel_x = 50 + column * 575
        parts.extend([
            f'<rect x="{panel_x}" y="118" width="535" height="548" rx="18" fill="#ffffff" stroke="#dde5f1"/>',
            f'<text x="{panel_x + 28}" y="158" font-family="Arial, sans-serif" font-size="22" font-weight="700" fill="#17233c">{name}</text>',
            f'<text x="{panel_x + 28}" y="184" font-family="Arial, sans-serif" font-size="14" fill="#60708d">3 vertices · 5 edges · 4 faces · 10 uses · 0 flaws</text>',
        ])
        drawing_left, drawing_top = panel_x + 44, 212
        drawing_width, drawing_height = 447, 380
        for step in range(5):
            y = drawing_top + step * drawing_height / 4
            parts.append(f'<line x1="{drawing_left}" y1="{y:.2f}" x2="{drawing_left + drawing_width}" y2="{y:.2f}" stroke="#edf1f7"/>')
        screen = panel_geometry(shape, drawing_left + 36, drawing_top + 22, drawing_width - 72, drawing_height - 44)
        for first, second in shape["guides"]:
            x1, y1 = screen(first)
            x2, y2 = screen(second)
            parts.append(f'<line x1="{x1:.3f}" y1="{y1:.3f}" x2="{x2:.3f}" y2="{y2:.3f}" stroke="#c6d0df" stroke-width="1" stroke-dasharray="5 5"/>')
        for first, second in shape["edges"]:
            x1, y1 = screen(first)
            x2, y2 = screen(second)
            parts.append(f'<line x1="{x1:.3f}" y1="{y1:.3f}" x2="{x2:.3f}" y2="{y2:.3f}" stroke="#245bc7" stroke-width="2.4" stroke-linecap="round"/>')
        for point in shape["points"]:
            x, y = screen(point)
            parts.append(f'<circle cx="{x:.3f}" cy="{y:.3f}" r="4.2" fill="#c26d26" stroke="#ffffff" stroke-width="1.4"/>')
        parts.append(f'<text x="{panel_x + 28}" y="632" font-family="Arial, sans-serif" font-size="13" fill="#60708d">Max vertex gap: {shape["gap"]:.3g} m</text>')
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def rasterize(svg_shapes, output: Path):
    from PIL import Image, ImageDraw, ImageFont

    width, height = 1200, 720
    image = Image.new("RGB", (width, height), "#f7f9fc")
    draw = ImageDraw.Draw(image)
    try:
        regular = ImageFont.truetype("arial.ttf", 15)
        small = ImageFont.truetype("arial.ttf", 13)
        title = ImageFont.truetype("arialbd.ttf", 30)
        heading = ImageFont.truetype("arialbd.ttf", 22)
    except OSError:
        regular = small = title = heading = ImageFont.load_default()
    draw.text((54, 30), "Dynedra Lab", fill="#17233c", font=title)
    draw.text((54, 72), "Native circular rim operations · exact B-rep edge sampling", fill="#60708d", font=regular)
    for column, kind in enumerate((6, 7)):
        name, _ = EXPECTED[kind]
        shape = svg_shapes[kind]
        panel_x = 50 + column * 575
        draw.rounded_rectangle((panel_x, 118, panel_x + 535, 666), radius=18, fill="#ffffff", outline="#dde5f1")
        draw.text((panel_x + 28, 132), name, fill="#17233c", font=heading)
        draw.text((panel_x + 28, 172), "3 vertices · 5 edges · 4 faces · 10 uses · 0 flaws", fill="#60708d", font=small)
        drawing_left, drawing_top = panel_x + 44, 212
        drawing_width, drawing_height = 447, 380
        for step in range(5):
            y = drawing_top + step * drawing_height / 4
            draw.line((drawing_left, y, drawing_left + drawing_width, y), fill="#edf1f7", width=1)
        screen = panel_geometry(shape, drawing_left + 36, drawing_top + 22, drawing_width - 72, drawing_height - 44)
        for first, second in shape["guides"]:
            draw.line((*screen(first), *screen(second)), fill="#c6d0df", width=1)
        for first, second in shape["edges"]:
            draw.line((*screen(first), *screen(second)), fill="#245bc7", width=3)
        for point in shape["points"]:
            x, y = screen(point)
            draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill="#c26d26", outline="#ffffff", width=1)
        draw.text((panel_x + 28, 620), f'Max vertex gap: {shape["gap"]:.3g} m', fill="#60708d", font=small)
    image.save(output, optimize=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build" / "dynlex.exe")
    parser.add_argument("--svg", type=Path, default=ROOT / "docs/images/dynedra-rim-operations.svg")
    parser.add_argument("--png", type=Path, default=ROOT / "docs/images/dynedra-rim-operations.png")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
    output_dir = Path(tempfile.mkdtemp(prefix="dynedra-drawing-", dir=ROOT / "build"))
    executable = output_dir / "drawing-O2.out"
    status, diagnostics, elapsed = run_process(
        [str(compiler), "tests/cadkernel/lab/drawing/main.dl", "-O2", "-o", str(executable)],
        timeout=180, cwd=ROOT, phase="compilation")
    if status != 0 or diagnostics.strip():
        raise SystemExit(f"drawing compile failed ({status}) after {elapsed:.3f}s\n{diagnostics}")
    status, output, run_elapsed = run_process([str(executable)], timeout=30, cwd=ROOT)
    if status != 0:
        raise SystemExit(f"drawing run failed ({status}) after {run_elapsed:.3f}s\n{output}")
    shapes = parse_output(output)
    args.svg.parent.mkdir(parents=True, exist_ok=True)
    args.png.parent.mkdir(parents=True, exist_ok=True)
    args.svg.write_text(svg_document(shapes, digest), encoding="utf-8")
    rasterize(shapes, args.png)
    if hashlib.sha256(compiler.read_bytes()).hexdigest() != digest:
        raise AssertionError("compiler changed while generating the drawing")
    print(f"Drawing verified from native O2 geometry: compile={elapsed:.3f}s run={run_elapsed:.3f}s")
    print(args.svg)
    print(args.png)


if __name__ == "__main__":
    main()
