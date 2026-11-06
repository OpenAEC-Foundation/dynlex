#!/usr/bin/env python3
"""Verify SVG import semantics and that the checked-in icon catalog is current."""
import unittest

from generate_ui_icons import ROOT, generate, parse_svg


def svg(body):
    return ('<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" '
            'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' + body + '</svg>')


class SvgImportTest(unittest.TestCase):
    def test_relative_and_repeated_commands(self):
        commands = parse_svg(svg('<path d="M2 3 4 5h3v2l-1-1"/>'))
        self.assertEqual([c[0] for c in commands], [0, 1, 1, 1, 1, 3])
        self.assertEqual(commands[-2][1:3], (6.0, 6.0))

    def test_curves_stay_curves(self):
        commands = parse_svg(svg('<path d="M2 3C4 5 6 7 8 9s2 3 4 5"/>'))
        self.assertEqual(commands[2], (2, 10.0, 11.0, 10.0, 12.0, 12.0, 14.0))

    def test_arc_keeps_exact_endpoint(self):
        commands = parse_svg(svg('<path d="M3 12a9 9 0 0 1 18 0"/>'))
        curves = [c for c in commands if c[0] == 2]
        self.assertEqual(len(curves), 2)
        self.assertEqual(curves[-1][-2:], (21.0, 12.0))

    def test_closed_contours_and_round_rectangles(self):
        commands = parse_svg(svg('<rect x="3" y="4" width="18" height="16" rx="2"/>'))
        self.assertEqual(commands[0][0], 4)
        self.assertEqual(sum(c[0] == 2 for c in commands), 4)
        self.assertEqual(commands[-1][0], 3)

    def test_unsupported_svg_is_an_error(self):
        with self.assertRaisesRegex(ValueError, 'transform'):
            parse_svg(svg('<path transform="rotate(45)" d="M1 1L2 2"/>'))
        with self.assertRaisesRegex(ValueError, 'fill'):
            parse_svg(svg('<circle cx="12" cy="12" r="4" fill="red"/>'))

    def test_generated_catalog_matches_svg_sources(self):
        self.assertEqual((ROOT / 'lib/ui_icons.dl').read_text(), generate())


if __name__ == '__main__':
    unittest.main()
