#!/usr/bin/env python3
"""Compile the stroked SVG icon catalog to static, resolution-independent data.

Curves remain cubic Beziers. This is an asset importer for the documented SVG
subset, not an SVG browser: unsupported styling or elements are errors.
"""
import argparse
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'lib/ui/icons'
NUMBER = r'[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?'
TOKEN = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]|' + NUMBER)


def arc(start, end, rx, ry, degrees, large, sweep):
    """SVG endpoint-parameterized elliptical arc, split into <=90 degree cubics."""
    if start == end:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [(1, *end)]
    angle = math.radians(degrees)
    co, si = math.cos(angle), math.sin(angle)
    dx, dy = (start[0] - end[0]) / 2, (start[1] - end[1]) / 2
    x, y = co * dx + si * dy, -si * dx + co * dy
    correction = x*x/(rx*rx) + y*y/(ry*ry)
    if correction > 1:
        rx *= math.sqrt(correction)
        ry *= math.sqrt(correction)
    numerator = max(0, rx*rx*ry*ry - rx*rx*y*y - ry*ry*x*x)
    factor = math.sqrt(numerator / (rx*rx*y*y + ry*ry*x*x))
    if bool(large) == bool(sweep):
        factor = -factor
    cx, cy = factor * rx * y / ry, -factor * ry * x / rx
    center = (co*cx - si*cy + (start[0]+end[0])/2,
              si*cx + co*cy + (start[1]+end[1])/2)
    begin = math.atan2((y-cy)/ry, (x-cx)/rx)
    finish = math.atan2((-y-cy)/ry, (-x-cx)/rx)
    delta = (finish - begin) % (2*math.pi)
    if not sweep:
        delta -= 2*math.pi
    count = max(1, math.ceil(abs(delta)/(math.pi/2)))
    step = delta / count

    def point(t):
        return (center[0] + co*rx*math.cos(t) - si*ry*math.sin(t),
                center[1] + si*rx*math.cos(t) + co*ry*math.sin(t))

    def tangent(t):
        return (-co*rx*math.sin(t) - si*ry*math.cos(t),
                -si*rx*math.sin(t) + co*ry*math.cos(t))

    result = []
    for index in range(count):
        a, b = begin + index*step, begin + (index+1)*step
        p, q, dp, dq = point(a), point(b), tangent(a), tangent(b)
        k = 4/3 * math.tan(step/4)
        result.append((2, p[0]+k*dp[0], p[1]+k*dp[1], q[0]-k*dq[0], q[1]-k*dq[1], *q))
    result[-1] = (*result[-1][:-2], *end)
    return result


def path_data(data):
    tokens = TOKEN.findall(data)
    if TOKEN.sub('', data).strip(' ,\t\n\r'):
        raise ValueError('Invalid SVG path data')
    index, command, previous = 0, '', ''
    current, start, control = (0., 0.), (0., 0.), (0., 0.)
    result, contour = [], []
    counts = dict(M=2, L=2, H=1, V=1, C=6, S=4, Q=4, T=2, A=7)

    def finish(closed):
        if contour:
            contour[0] = (4 if closed else 0, *contour[0][1:])
            result.extend(contour)
            result.append((3,))
            contour.clear()

    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
            if command.upper() == 'Z':
                finish(True)
                current = start
                previous = 'Z'
                command = ''
                continue
        op = command.upper()
        if op not in counts:
            raise ValueError('SVG path requires a command')
        size = counts[op]
        values = list(map(float, tokens[index:index+size]))
        if len(values) != size:
            raise ValueError('Incomplete SVG path command')
        index += size
        relative = command.islower()

        def point(at):
            return (values[at] + (current[0] if relative else 0),
                    values[at+1] + (current[1] if relative else 0))

        end = current
        if op == 'M':
            finish(False)
            end = start = point(0)
            contour.append((0, *end))
            command = 'l' if relative else 'L'
        elif op in ('L', 'H', 'V'):
            if op == 'L':
                end = point(0)
            elif op == 'H':
                end = (values[0] + (current[0] if relative else 0), current[1])
            else:
                end = (current[0], values[0] + (current[1] if relative else 0))
            contour.append((1, *end))
        elif op in ('C', 'S'):
            first = point(0) if op == 'C' else (
                (2*current[0]-control[0], 2*current[1]-control[1]) if previous in ('C', 'S') else current)
            control = point(2 if op == 'C' else 0)
            end = point(4 if op == 'C' else 2)
            contour.append((2, *first, *control, *end))
        elif op in ('Q', 'T'):
            q = point(0) if op == 'Q' else (
                (2*current[0]-control[0], 2*current[1]-control[1]) if previous in ('Q', 'T') else current)
            end = point(2 if op == 'Q' else 0)
            contour.append((2, *(current[i]+2/3*(q[i]-current[i]) for i in range(2)),
                            *(end[i]+2/3*(q[i]-end[i]) for i in range(2)), *end))
            control = q
        else:
            if values[3] not in (0, 1) or values[4] not in (0, 1):
                raise ValueError('SVG arc flags must be 0 or 1')
            end = point(5)
            contour.extend(arc(current, end, *values[:5]))
        current, previous = end, op
    finish(False)
    return result


def parse_svg(text):
    root = ET.fromstring(text)
    if root.get('viewBox') != '0 0 24 24':
        raise ValueError('Icon viewBox must be 0 0 24 24')
    allowed = {'xmlns', 'width', 'height', 'viewBox', 'fill', 'stroke', 'stroke-width',
               'stroke-linecap', 'stroke-linejoin', 'd', 'x', 'y', 'x1', 'y1', 'x2',
               'y2', 'rx', 'ry', 'cx', 'cy', 'r', 'points'}
    commands = []
    for node in root.iter():
        for key in node.attrib:
            if key not in allowed:
                raise ValueError(f'Unsupported SVG attribute: {key}')
        for key, expected in [('fill', 'none'), ('stroke', 'currentColor'), ('stroke-width', '2'),
                              ('stroke-linecap', 'round'), ('stroke-linejoin', 'round')]:
            if node.get(key, root.get(key)) != expected:
                raise ValueError(f'Unsupported SVG {key}: expected {expected}')
        kind = node.tag.rsplit('}', 1)[-1]
        v = lambda name, default=0: float(node.get(name, default))
        if kind == 'svg':
            continue
        if kind == 'path':
            commands.extend(path_data(node.attrib['d']))
        elif kind == 'line':
            commands.extend([(0, v('x1'), v('y1')), (1, v('x2'), v('y2')), (3,)])
        elif kind in ('polyline', 'polygon'):
            points = list(map(float, re.findall(NUMBER, node.attrib['points'])))
            if len(points) < 4 or len(points) % 2:
                raise ValueError('Invalid SVG point list')
            commands.append((4 if kind == 'polygon' else 0, *points[:2]))
            commands.extend((1, *points[i:i+2]) for i in range(2, len(points), 2))
            commands.append((3,))
        elif kind in ('circle', 'ellipse'):
            x, y = v('cx'), v('cy')
            rx, ry = (v('r'), v('r')) if kind == 'circle' else (v('rx'), v('ry'))
            commands.append((4, x+rx, y))
            commands.extend(arc((x+rx, y), (x-rx, y), rx, ry, 0, 0, 1))
            commands.extend(arc((x-rx, y), (x+rx, y), rx, ry, 0, 0, 1))
            commands.append((3,))
        elif kind == 'rect':
            x, y, w, h = v('x'), v('y'), v('width'), v('height')
            rx, ry = min(v('rx', v('ry')), w/2), min(v('ry', v('rx')), h/2)
            commands.append((4, x+rx, y))
            for end, corner in [((x+w-rx, y), (x+w, y+ry)), ((x+w, y+h-ry), (x+w-rx, y+h)),
                                ((x+rx, y+h), (x, y+h-ry)), ((x, y+ry), (x+rx, y))]:
                commands.append((1, *end))
                commands.extend(arc(end, corner, rx, ry, 0, 0, 1))
            commands.append((3,))
        else:
            raise ValueError(f'Unsupported SVG element: {kind}')
    if not commands:
        raise ValueError('Empty SVG icon')
    return commands


def number(value):
    value = round(value, 8)
    return f'{value:.8f}'.rstrip('0').rstrip('.') + ('.0' if value == int(value) else '')


def generate():
    lines = ['# Generated from lib/ui/icons/*.svg by scripts/generate_ui_icons.py.',
             '# Edit the SVG source; cubic curves are rasterized at the displayed size.',
             '# Lucide 0.468.0. Preserve this notice when distributing the generated library.', '#']
    lines.extend('# ' + line if line else '#' for line in (ASSETS / 'LICENSE').read_text().splitlines())
    lines += ['', 'import lib/ui_svg_types.dl', '', 'to get the UI icon named {C string:name}:']
    catalog = json.loads((ASSETS / 'catalog.json').read_text())
    for name, asset in catalog.items():
        commands = parse_svg((ASSETS / f'{asset}.svg').read_text())
        expressions = []
        for op, *points in commands:
            if op in (0, 4):
                expression = f'SVG {"open" if op == 0 else "closed"} {number(points[0])} {number(points[1])}'
            elif op == 1:
                expression = f'SVG line {number(points[0])} {number(points[1])}'
            elif op == 2:
                expression = 'SVG curve ' + ' '.join(map(number, points))
            else:
                expression = 'SVG end'
            expressions.append(expression)
        lines += [f'    if name = "{name}":', '        return a UI SVG icon from [']
        lines += [f'            {expression}{"," if index + 1 < len(expressions) else ""}'
                  for index, expression in enumerate(expressions)]
        lines.append('        ]')
    lines += ['    print line ("Unknown UI icon: " + (the string form of name))',
              '    abort the program', '    return a new UI SVG icon', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target, generated = ROOT / 'lib/ui_icons.dl', generate()
    if args.check:
        if target.read_text() != generated:
            raise SystemExit('SVG icon data is out of date; run scripts/generate_ui_icons.py')
    else:
        target.write_text(generated)
