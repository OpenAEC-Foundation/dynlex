// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line};

fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|value| value.parse().unwrap()).collect();
    let point3 = |index: usize| [values[index], values[index + 1], values[index + 2]];
    let point2 = |index: usize| [values[index], values[index + 1]];
    let frame = Plane::from_axes(point3(0), point3(3), point3(6));
    let pivot = point3(9);
    let axis = point3(12);
    let angle = values[15];
    let count = values[16] as usize;
    let mut cursor = 17;
    let mut profile = Vec::with_capacity(count);
    for _ in 0..count {
        let kind = values[cursor] as usize;
        cursor += 1;
        let piece = match kind {
            0 => {
                let result = Curve2::Line(Line { start: point2(cursor), end: point2(cursor + 2) });
                cursor += 4;
                result
            }
            1 => {
                let result = Curve2::Circle(Circle { centre: point2(cursor), radius: values[cursor + 2] });
                cursor += 3;
                result
            }
            2 => {
                let result = Curve2::Arc(Arc {
                    centre: point2(cursor), radius: values[cursor + 2],
                    start_angle: values[cursor + 3], end_angle: values[cursor + 4],
                });
                cursor += 5;
                result
            }
            _ => panic!("unsupported probe kind"),
        };
        profile.push(piece);
    }
    assert_eq!(cursor, values.len());
    let result = revolve(frame, &profile, pivot, axis, angle);
    flag(result.is_some());
    if let Some(body) = result {
        emit(&body);
    }
}
