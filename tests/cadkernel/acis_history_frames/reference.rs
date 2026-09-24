// SPDX-License-Identifier: MPL-2.0
// verify.py includes the exact five helpers from pinned src/acis/history.rs.
use acadrust::types::{Matrix3, Vector3};
use cadkernel::brep::Placement;
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::{PlanarCurve, Plane, Vec3};

#[derive(Debug)]
enum HistoryRebuildError {
    InvalidParameters,
    InvalidTransform,
}

include!("history_helpers.rs");

fn bits(value: f64) -> String {
    if value.is_nan() { return "nan".into(); }
    let word = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", (word >> 32) as u32, word as u32)
}

fn vector(value: [f64; 3]) -> String {
    format!("{},{},{}", bits(value[0]), bits(value[1]), bits(value[2]))
}

fn vector2(value: [f64; 2]) -> String {
    format!("{},{}", bits(value[0]), bits(value[1]))
}

fn placed(value: Placement) -> String {
    format!("{}|{}|{}|{}", vector(value.x_axis), vector(value.y_axis), vector(value.z_axis), vector(value.origin))
}

fn curve(value: PlanarCurve) -> String {
    let Curve::Line(line) = value.curve else { panic!("expected line") };
    format!("{}|{}|{}|{}|{}", vector(value.plane.origin), vector(value.plane.x_axis), vector(value.plane.y_axis), vector2(line.start), vector2(line.end))
}

fn number(args: &[String], index: usize) -> f64 {
    args[index].parse::<f64>().expect("number")
}

fn point(args: &[String], index: usize) -> Vector3 {
    Vector3::new(number(args, index), number(args, index + 1), number(args, index + 2))
}

fn transform(args: &[String], offset: usize) -> [f64; 16] {
    std::array::from_fn(|index| number(args, offset + index))
}

fn failure(error: HistoryRebuildError) -> &'static str {
    match error {
        HistoryRebuildError::InvalidTransform => "invalid-transform",
        HistoryRebuildError::InvalidParameters => "invalid-parameters",
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args[0].as_str() {
        "placement" => match placement(transform(&args, 1)) {
            Ok(value) => println!("{}", placed(value)),
            Err(error) => println!("{}", failure(error)),
        },
        "compose" => {
            let outer = placement(transform(&args, 1)).expect("outer");
            let inner = placement(transform(&args, 17)).expect("inner");
            println!("{}", placed(compose_placements(outer, inner)));
        }
        "ocs" => match ocs_plane(point(&args, 1), number(&args, 4)) {
            Ok(value) => println!("{}|{}|{}", vector(value.origin), vector(value.x_axis), vector(value.y_axis)),
            Err(error) => println!("{}", failure(error)),
        },
        "straight" => match straight_curve(point(&args, 1), point(&args, 4)) {
            Ok(value) => println!("{}", curve(value)),
            Err(error) => println!("{}", failure(error)),
        },
        "placed" => {
            let source = straight_curve(Vector3::new(1.0, 2.0, 3.0), Vector3::new(4.0, 6.0, 3.0)).unwrap();
            match placed_curve(source, transform(&args, 1)) {
                Ok(value) => println!("{}", curve(value)),
                Err(error) => println!("{}", failure(error)),
            }
        }
        _ => panic!("unknown mode"),
    }
}
