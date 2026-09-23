// SPDX-License-Identifier: MPL-2.0
include!("emit.rs");

pub use cadkernel::brep::{
    operation_tolerance, revolve, Body, ChamferError, Curve3, EdgeKey, FaceKey, Surface,
};
mod geom2d {
    pub use cadkernel::geom2d::*;
}
mod space {
    pub use cadkernel::space::*;
}
#[path = "chamfer_profile_source.rs"]
mod chamfer_profile;
#[path = "chamfer_circular_source.rs"]
mod chamfer_circular;

use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line};

fn problem_code(problem: ChamferError, selected: &[EdgeKey]) -> (usize, usize) {
    match problem {
        ChamferError::EdgeOutsideBaseFace(edge) => (1, usize::from(selected.contains(&edge))),
        ChamferError::DistanceTooLargeOrInteracting => (2, 0),
        ChamferError::UnsupportedBodySurface => (3, 0),
        ChamferError::InvalidResult => (4, 0),
        other => panic!("unexpected circular chamfer error: {other:?}"),
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
                    frame: Plane::XY, radius: value.abs().max(1.0),
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
    let base_request = values[cursor];
    let base_distance = values[cursor + 1];
    let other_distance = values[cursor + 2];
    let damage_code = values[cursor + 3] as i32;
    let damage_value = values[cursor + 4];
    assert_eq!(cursor + 5, values.len());

    let mut body = revolve(frame, &profile, pivot, axis, angle).unwrap_or_default();
    let edges: Vec<_> = body.edge_keys().collect();
    let faces: Vec<_> = body.face_keys().collect();
    let selected: Vec<_> = requested.into_iter().filter_map(|value| {
        (!edges.is_empty()).then(|| edges[(value.abs() as usize) % edges.len()])
    }).collect();
    let base = (!faces.is_empty()).then(|| faces[(base_request.abs() as usize) % faces.len()]);
    damage(&mut body, damage_code, damage_value);
    let result = base.and_then(|base_face| {
        chamfer_circular::chamfer_circular(
            &body, &selected, base_face, base_distance, other_distance,
        )
    });
    match result {
        None => { flag(false); flag(false); exact(0); exact(0); }
        Some(Err(problem)) => {
            let (kind, edge) = problem_code(problem, &selected);
            flag(true); flag(false); exact(kind); exact(edge);
        }
        Some(Ok(result)) => {
            flag(true); flag(true); exact(0); exact(0); emit(&result); number(result.worst_vertex_gap());
        }
    }
}
