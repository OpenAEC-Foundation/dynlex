// SPDX-License-Identifier: MPL-2.0
use std::collections::HashMap;

pub use cadkernel::brep::{ChamferError, EdgeKey, FaceKey};

mod geom2d {
    pub use cadkernel::geom2d::*;
}

#[path = "chamfer_profile_source.rs"]
mod source;

use cadkernel::brep::make;
use geom2d::{Arc, Circle, Curve, Line};

fn number(value: f64) {
    println!("{value:.17e}");
}

fn emit(result: Result<Vec<Curve>, ChamferError>) {
    match result {
        Err(error) => {
            println!("0");
            let (kind, edge) = match error {
                ChamferError::EdgeOutsideBaseFace(edge) => (1, edge.slot() as i64),
                ChamferError::DistanceTooLargeOrInteracting => (2, 0),
                ChamferError::UnsupportedBodySurface => (3, 0),
                ChamferError::InvalidResult => (4, 0),
                other => panic!("unexpected chamfer error: {other:?}"),
            };
            println!("{kind}");
            println!("{edge}");
        }
        Ok(curves) => {
            println!("1");
            println!("{}", curves.len());
            for curve in curves {
                match curve {
                    Curve::Line(line) => {
                        println!("0");
                        number(line.start[0]);
                        number(line.start[1]);
                        number(line.end[0]);
                        number(line.end[1]);
                    }
                    Curve::Arc(arc) => {
                        println!("1");
                        number(arc.centre[0]);
                        number(arc.centre[1]);
                        number(arc.radius);
                        number(arc.start_angle);
                        number(arc.end_angle);
                    }
                    other => panic!("unexpected output curve: {other:?}"),
                }
            }
        }
    }
}

fn main() {
    let values: Vec<f64> = std::env::args()
        .skip(1)
        .map(|value| value.parse().expect("number"))
        .collect();
    let segment_count = values[0] as usize;
    let point_count = values[1] as usize;
    let face_count = values[2] as usize;
    let base_distance = values[3];
    let other_distance = values[4];
    let tolerance = values[5];
    let avoid_axis = values[6] != 0.0;

    let body = make::cuboid([0.0; 3], [2.0, 3.0, 4.0]).unwrap();
    let face_keys: Vec<_> = body.face_keys().collect();
    let edge_keys: Vec<_> = body.edge_keys().collect();
    let base_face = face_keys[0];
    let mut segments = Vec::with_capacity(segment_count);
    let mut selected = HashMap::new();
    let mut face_codes = Vec::with_capacity(segment_count);
    for index in 0..segment_count {
        let offset = 7 + index * 8;
        let kind = values[offset] as i32;
        let a = values[offset + 1];
        let b = values[offset + 2];
        let c = values[offset + 3];
        let d = values[offset + 4];
        let e = values[offset + 5];
        let curve = match kind {
            0 => Curve::Line(Line {
                start: [a, b],
                end: [c, d],
            }),
            1 => Curve::Arc(Arc {
                centre: [a, b],
                radius: c,
                start_angle: d,
                end_angle: e,
            }),
            2 => Curve::Circle(Circle {
                centre: [a, b],
                radius: c,
            }),
            _ => panic!("curve kind"),
        };
        segments.push(curve);
        face_codes.push(values[offset + 6] as i32);
        let edge = values[offset + 7] as i32;
        if edge >= 0 {
            selected.insert(index, edge_keys[edge as usize % edge_keys.len()]);
        }
    }
    let points = vec![[0.0, 0.0]; point_count];
    let mut faces = Vec::with_capacity(face_count);
    for index in 0..face_count {
        let code = *face_codes.get(index).unwrap_or(&0);
        faces.push(if code < 0 {
            None
        } else {
            Some(face_keys[code as usize % face_keys.len()])
        });
    }
    emit(source::chamfer_profile(
        &segments,
        &points,
        &selected,
        &faces,
        base_face,
        base_distance,
        other_distance,
        tolerance,
        avoid_axis,
    ));
}
