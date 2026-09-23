// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/sweep.rs at
// 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{revolve_surface, revolve_surface_region, Body, Surface};
use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line};
use cadkernel::space::Plane;

fn emit(value: f64) {
    println!("{value:.17e}");
}

fn emit_body(body: &Body) {
    for count in [
        body.vertices.len(),
        body.edges.len(),
        body.coedges.len(),
        body.loops.len(),
        body.faces.len(),
        body.shells.len(),
        body.lumps.len(),
        body.surfaces.len(),
        body.curves.len(),
        body.roots.len(),
        body.validate().len(),
    ] {
        emit(count as f64);
    }
    for kind in 0..6 {
        let count = body
            .surfaces
            .iter()
            .filter(|(_, surface)| {
                matches!(
                    (kind, *surface),
                    (0, Surface::Plane(_))
                        | (1, Surface::Cylinder(_))
                        | (2, Surface::Cone(_))
                        | (3, Surface::Sphere(_))
                        | (4, Surface::Torus(_))
                        | (5, Surface::Nurbs(_))
                )
            })
            .count();
        emit(count as f64);
    }
    emit(body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 1).count() as f64);
    emit(body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count() as f64);
    emit(body.coedges.iter().filter(|(_, coedge)| coedge.pcurve.is_some()).count() as f64);
    for root in &body.roots {
        let lump = body.lumps.get(*root).expect("live lump");
        emit(lump.shells.len() as f64);
        for shell in &lump.shells {
            emit(body.shells.get(*shell).expect("live shell").faces.len() as f64);
        }
    }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point {
            emit(coordinate);
        }
    }
}

fn main() {
    let values: Vec<f64> = std::env::args()
        .skip(1)
        .map(|value| value.parse().expect("numeric argument"))
        .collect();
    let mut cursor = 0;
    let mut next = || {
        let value = values[cursor];
        cursor += 1;
        value
    };
    let mode = next() as u8;
    let pivot = [next(), next(), next()];
    let axis = [next(), next(), next()];
    let angle = next();
    let profile_count = next() as usize;
    let mut profiles = Vec::with_capacity(profile_count);
    for _ in 0..profile_count {
        let piece_count = next() as usize;
        let mut profile = Vec::with_capacity(piece_count);
        for _ in 0..piece_count {
            let piece = match next() as u8 {
                0 => Curve2::Line(Line {
                    start: [next(), next()],
                    end: [next(), next()],
                }),
                1 => Curve2::Circle(Circle {
                    centre: [next(), next()],
                    radius: next(),
                }),
                2 => Curve2::Arc(Arc {
                    centre: [next(), next()],
                    radius: next(),
                    start_angle: next(),
                    end_angle: next(),
                }),
                _ => panic!("unsupported input kind"),
            };
            profile.push(piece);
        }
        profiles.push(profile);
    }
    assert_eq!(cursor, values.len(), "input consumed exactly");
    let plane = Plane::orthonormal([0.0; 3], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0])
        .expect("half-plane");
    let result = match mode {
        0 => profiles
            .first()
            .and_then(|profile| revolve_surface(plane, profile, pivot, axis, angle)),
        1 => revolve_surface_region(plane, &profiles, pivot, axis, angle),
        _ => panic!("unsupported operation"),
    };
    emit(result.is_some() as u8 as f64);
    if let Some(body) = result {
        emit_body(&body);
    }
}
