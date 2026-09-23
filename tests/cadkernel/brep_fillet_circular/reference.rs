// SPDX-License-Identifier: MPL-2.0
include!("emit.rs");

pub use cadkernel::brep::{operation_tolerance, revolve, Body, Curve3, EdgeKey, FilletError, Surface};
mod blend {
    pub use cadkernel::brep::FilletError;
}
mod geom2d {
    pub use cadkernel::geom2d::*;
}
mod space {
    pub use cadkernel::space::*;
}
#[path = "fillet_circular_source.rs"]
mod fillet_circular;

use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line};

fn problem_code(problem: FilletError) -> usize {
    match problem {
        FilletError::UnsupportedExistingFillet => 13,
        FilletError::RadiusTooLargeOrInteracting => 14,
        FilletError::InvalidResult => 16,
        other => panic!("unexpected circular fillet error: {other:?}"),
    }
}

fn damage(body: &mut Body, code: i32, value: f64) {
    let edges: Vec<_> = body.edge_keys().collect();
    let faces: Vec<_> = body.face_keys().collect();
    match code {
        1 => body.roots.clear(),
        2 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get_mut(*key)) {
                edge.coedges.clear();
            }
        }
        3 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get_mut(*key)) {
                if let Some(key) = edge.coedges.first().copied() { edge.coedges.push(key); }
            }
        }
        4 => {
            if let Some(face) = faces.first().and_then(|key| body.faces.get(*key)) {
                let key = face.surface;
                *body.surfaces.get_mut(key).unwrap() = Surface::Sphere(Sphere {
                    frame: Plane::XY,
                    radius: value.abs().max(1.0),
                });
            }
        }
        5 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get(*key)) {
                let key = edge.curve;
                *body.curves.get_mut(key).unwrap() = Curve3::Line(Line3 {
                    origin: [0.0, 0.0, 0.0], direction: [1.0, 0.0, 0.0],
                });
            }
        }
        _ => {}
    }
}

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
    let selection_count = values[cursor] as usize;
    cursor += 1;
    let requested = values[cursor..cursor + selection_count].to_vec();
    cursor += selection_count;
    let radius = values[cursor];
    let damage_code = values[cursor + 1] as i32;
    let damage_value = values[cursor + 2];
    assert_eq!(cursor + 3, values.len());

    let mut body = revolve(frame, &profile, pivot, axis, angle).unwrap_or_default();
    let edges: Vec<_> = body.edge_keys().collect();
    let selected: Vec<_> = requested.into_iter().filter_map(|value| {
        (!edges.is_empty()).then(|| edges[(value.abs() as usize) % edges.len()])
    }).collect();
    damage(&mut body, damage_code, damage_value);
    match fillet_circular::fillet_circular(&body, &selected, radius) {
        None => { flag(false); flag(false); exact(0); }
        Some(Err(problem)) => { flag(true); flag(false); exact(problem_code(problem)); }
        Some(Ok(result)) => {
            flag(true); flag(true); exact(0); emit(&result); number(result.worst_vertex_gap());
        }
    }
}
