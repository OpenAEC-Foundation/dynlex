// SPDX-License-Identifier: MPL-2.0
// The driver injects pinned loft definitions, methods, and function bodies.
pub use cadkernel::{geom2d, space};
use geom2d::{Arc, Circle, Curve, Ellipse, EllipseArc, Line, NurbsCurve, Ray, XLine};
use space::{Plane, Vec3};
use nurbs_builder::{RationalCurve2, RationalCurve3};
use std::f64::consts::FRAC_PI_2;
use std::fmt;

// PINNED_SOURCE_DEFINITIONS
// PINNED_SOURCE_METHODS
// PINNED_SOURCE_FUNCTIONS

fn next(args: &[String], cursor: &mut usize) -> f64 {
    let value = args[*cursor].parse::<f64>().unwrap();
    *cursor += 1;
    value
}
fn count(args: &[String], cursor: &mut usize) -> usize { next(args, cursor) as usize }
fn point2(args: &[String], cursor: &mut usize) -> [f64; 2] {
    [next(args, cursor), next(args, cursor)]
}
fn point3(args: &[String], cursor: &mut usize) -> [f64; 3] {
    [next(args, cursor), next(args, cursor), next(args, cursor)]
}
fn read_curve(args: &[String], cursor: &mut usize) -> Curve {
    match count(args, cursor) {
        0 => Curve::Line(Line { start: point2(args, cursor), end: point2(args, cursor) }),
        1 => Curve::Circle(Circle { centre: point2(args, cursor), radius: next(args, cursor) }),
        2 => Curve::Arc(Arc { centre: point2(args, cursor), radius: next(args, cursor),
            start_angle: next(args, cursor), end_angle: next(args, cursor) }),
        3 => Curve::Ellipse(EllipseArc { ellipse: Ellipse {
            centre: point2(args, cursor), major_radius: next(args, cursor),
            minor_radius: next(args, cursor), major_axis: point2(args, cursor),
        }, start_parameter: next(args, cursor), end_parameter: next(args, cursor) }),
        4 => {
            let degree = count(args, cursor);
            let control_count = count(args, cursor);
            let controls = (0..control_count).map(|_| point2(args, cursor)).collect();
            let knot_count = count(args, cursor);
            let knots = (0..knot_count).map(|_| next(args, cursor)).collect();
            let weight_count = count(args, cursor);
            let weights = (0..weight_count).map(|_| next(args, cursor)).collect();
            Curve::Nurbs(NurbsCurve::new_strict(degree, controls, knots, weights)
                .expect("the probe requires a constructible spline"))
        }
        5 => Curve::Ray(Ray { origin: point2(args, cursor), direction: point2(args, cursor) }),
        6 => Curve::XLine(XLine { base: point2(args, cursor), direction: point2(args, cursor) }),
        kind => panic!("unsupported input curve kind {kind}"),
    }
}
fn emit(value: f64) { println!("{value:.17e}"); }
fn refusal(code: f64) { emit(0.0); emit(code); emit(0.0); emit(0.0); }
fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let mut cursor = 0;
    let cyclic = next(&args, &mut cursor) != 0.0;
    let matching = next(&args, &mut cursor) != 0.0;
    let mode = next(&args, &mut cursor) as i32;
    let periodic = next(&args, &mut cursor) != 0.0;
    let start_angle = next(&args, &mut cursor);
    let end_angle = next(&args, &mut cursor);
    let start_magnitude = next(&args, &mut cursor);
    let end_magnitude = next(&args, &mut cursor);
    let wire_index = next(&args, &mut cursor) as isize;
    let band = next(&args, &mut cursor) as isize;
    let section_count = count(&args, &mut cursor);
    let sections = (0..section_count).map(|_| {
        let plane = Plane::from_axes(point3(&args, &mut cursor), point3(&args, &mut cursor), point3(&args, &mut cursor));
        let closed = next(&args, &mut cursor) != 0.0;
        let wire_count = count(&args, &mut cursor);
        let wires = (0..wire_count).map(|_| {
            let piece_count = count(&args, &mut cursor);
            (0..piece_count).map(|_| read_curve(&args, &mut cursor)).collect::<Vec<_>>()
        }).collect::<Vec<_>>();
        LoftSection::Profile { plane, wires, closed }
    }).collect::<Vec<_>>();
    assert_eq!(cursor, args.len());
    let mut options = LoftOptions::default();
    options.closed = cyclic;
    options.align_direction = matching;
    options.normals = mode;
    options.periodic = periodic;
    options.start_draft_angle = start_angle;
    options.end_draft_angle = end_angle;
    options.start_magnitude = start_magnitude;
    options.end_magnitude = end_magnitude;
    if !(0..=6).contains(&mode) || [start_angle, end_angle, start_magnitude, end_magnitude].iter().any(|v| !v.is_finite())
        || start_magnitude < 0.0 || end_magnitude < 0.0 || sections.len() < 2 || cyclic && sections.len() < 3 {
        refusal(1.0); return;
    }
    let Ok(sections) = prepared(&sections, options) else { refusal(2.0); return; };
    let count = sections.len();
    if wire_index < 0 || wire_index as usize >= sections[0].wires.len() { refusal(3.0); return; }
    let bands = count - usize::from(!cyclic);
    if band < 0 || band as usize >= bands { refusal(3.0); return; }
    let wire_index = wire_index as usize;
    let band = band as usize;
    let mut lengths = Vec::new();
    for step in 0..bands {
        let next = (step + 1) % count;
        let mut length = (sections[next].centre - sections[step].centre).length();
        if length < 1e-9 {
            length = (0..16).map(|i| distance(sections[step].wires[0].point(i as f64 / 16.0),
                sections[next].wires[0].point(i as f64 / 16.0))).sum::<f64>() / 16.0;
        }
        if length <= 1e-10 { refusal(4.0); return; }
        lengths.push(length);
    }
    let mut breaks = vec![0.0, 1.0];
    for section in &sections { breaks.extend(section.wires[wire_index].spans.iter().map(|s| s.start)); }
    unique(&mut breaks);
    let mut curves = Vec::new();
    for section in &sections {
        let wire = &section.wires[wire_index];
        let mut row = Vec::new();
        for pair in breaks.windows(2) {
            let Ok(curve) = wire.part(pair[0], pair[1]) else { refusal(5.0); return; };
            row.push(curve.unit_end_weights());
        }
        curves.push(row);
    }
    for column in 0..breaks.len() - 1 {
        let degree = curves.iter().map(|row| row[column].degree()).max().unwrap();
        for row in &mut curves { row[column] = row[column].elevated(degree); }
    }
    emit(1.0); emit(0.0); emit(lengths.len() as f64);
    for length in &lengths { emit(*length); }
    emit((breaks.len() - 1) as f64);
    for column in 0..breaks.len() - 1 {
        let patch = base_patch(&curves, column, band, &sections, &lengths, options);
        emit(breaks[column]); emit(breaks[column + 1]);
        emit(patch.control.len() as f64);
        for row in patch.control {
            emit(row.len() as f64);
            for point in row { for value in point { emit(value); } }
        }
    }
}
