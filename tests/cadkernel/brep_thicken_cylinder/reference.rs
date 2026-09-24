// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{analytic_mass_properties, revolve_surface, thicken, Body};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;
use std::f64::consts::{PI, TAU};

fn emit(value: f64) { println!("{value:.17e}"); }

fn output(body: Body) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
        body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
        body.coedges.iter().filter(|(_, use_node)| use_node.pcurve.is_some()).count(),
        body.faces.iter().filter(|(_, face)| face.forward).count(),
    ] { emit(count as f64); }
    let mass = analytic_mass_properties(&body).expect("analytic cylinder sector mass");
    emit(mass.volume);
    for coordinate in mass.centroid { emit(coordinate); }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let angles = [PI / 2.0, PI * 1.5, PI * 1.99, TAU, -PI * 1.5];
    let angle = if case < 10 { angles[case / 2] } else { PI / 2.0 };
    let section = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
    let sheet = revolve_surface(
        section,
        &[Curve::Line(Line { start: [4.0, 0.0], end: [4.0, 3.0] })],
        [0.0; 3], [0.0, 0.0, 1.0], angle,
    ).expect("cylindrical sheet");
    let face = sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap();
    let distance = match case {
        0..=9 => if case % 2 == 0 { 0.5 } else { -0.5 },
        10 => 0.0,
        11 => if face.forward { -5.0 } else { 5.0 },
        _ => panic!("unknown case"),
    };
    let result = thicken(&sheet, distance);
    emit(result.is_ok() as u8 as f64);
    if let Ok(body) = result { output(body); }
}
