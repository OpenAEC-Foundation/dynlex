// SPDX-License-Identifier: MPL-2.0
// The driver injects the pinned Rust function bodies at the marker below.
pub use cadkernel::{geom2d, space};
use geom2d::{Arc, Circle, Curve, Ellipse, EllipseArc, Line, NurbsCurve, Ray, XLine};
use space::{Plane, Vec3};
use nurbs_builder::{RationalCurve2, RationalCurve3};

type H = [f64; 4];
#[derive(Clone)]
struct Bezier { control: Vec<H> }
#[derive(Clone)]
struct Span { start: f64, end: f64, curve: Bezier }
#[derive(Clone)]
struct Wire { spans: Vec<Span>, closed: bool }
struct LoftError(String);
fn error(message: &str) -> LoftError { LoftError(message.to_owned()) }

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
        "A loft section needs a valid plane and an outer wire; holes require closed profiles." => 1.0,
        "A loft wire is empty." => 2.0,
        "A loft section contains an unsupported curve." => 3.0,
        "Loft section edges do not form a connected wire." => 4.0,
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
fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let mut cursor = 0;
    let plane = Plane::from_axes(point3(&args, &mut cursor), point3(&args, &mut cursor), point3(&args, &mut cursor));
    let closed = next(&args, &mut cursor) != 0.0;
    let wire_count = count(&args, &mut cursor);
    let wires = (0..wire_count).map(|_| {
        let piece_count = count(&args, &mut cursor);
        (0..piece_count).map(|_| read_curve(&args, &mut cursor)).collect::<Vec<_>>()
    }).collect::<Vec<_>>();
    assert_eq!(cursor, args.len());
    // This guard and map are the Profile arm of prepared(), pinned beside section_wire.
    let result: Result<Vec<Wire>, LoftError> = if plane.normal().is_none() || wires.is_empty() || (!closed && wires.len() != 1) {
        Err(error("A loft section needs a valid plane and an outer wire; holes require closed profiles."))
    } else {
        wires.iter().map(|wire| section_wire(&plane, wire, closed)).collect()
    };
    match result {
        Err(failure) => { emit(0.0); emit(failure_code(failure)); emit(0.0); }
        Ok(wires) => {
            emit(1.0); emit(0.0); emit(wires.len() as f64);
            for wire in wires {
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
