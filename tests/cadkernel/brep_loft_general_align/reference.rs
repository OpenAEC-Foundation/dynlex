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
fn failure_code(failure: LoftError) -> f64 {
    match failure.0.as_str() {
        "LOFT requires at least two cross sections." => 1.0,
        "A closed loft requires at least three cross sections." => 2.0,
        "A loft section needs a valid plane and an outer wire; holes require closed profiles." => 3.0,
        "A loft wire is empty." => 4.0,
        "A loft section contains an unsupported curve." => 5.0,
        "Loft section edges do not form a connected wire." => 6.0,
        "All loft profiles must have matching open/closed topology and hole counts." => 7.0,
        "A loft curve interval crosses an unsplit knot." => 8.0,
        "A loft section contains an invalid spline." => 11.0,
        "Loft section weights must be finite and positive." => 12.0,
        "A loft section has an invalid knot domain." => 13.0,
        "Spline knot refinement failed." => 14.0,
        "Spline knot refinement encountered a zero span." => 15.0,
        "A loft section contains no nonzero curve spans." => 16.0,
        message => panic!("unmapped source refusal: {message}"),
    }
}
fn emit(value: f64) { println!("{value:.17e}"); }
fn emit_point(value: [f64; 3]) { for coordinate in value { emit(coordinate); } }
fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let mut cursor = 0;
    let cyclic = next(&args, &mut cursor) != 0.0;
    let matching = next(&args, &mut cursor) != 0.0;
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
    match prepared(&sections, options) {
        Err(failure) => { emit(0.0); emit(failure_code(failure)); emit(0.0); }
        Ok(sections) => {
            emit(1.0); emit(0.0); emit(sections.len() as f64);
            for section in sections {
                let plane = section.plane.unwrap();
                emit_point(plane.origin);
                emit_point(plane.x_axis);
                emit_point(plane.y_axis);
                emit_point(section.centre.to_array());
                emit(section.wires.len() as f64);
                for wire in section.wires {
                    emit(wire.closed as u8 as f64);
                    emit(wire.spans.len() as f64);
                    for span in wire.spans {
                        emit(span.start); emit(span.end);
                        emit(span.curve.control.len() as f64);
                        for h in span.curve.control { for coordinate in h { emit(coordinate); } }
                    }
                }
            }
        }
    }
}
