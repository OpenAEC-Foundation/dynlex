// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{make, revolve_surface, thicken, Body, Surface};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;
use std::f64::consts::{FRAC_PI_2, PI, TAU};

fn emit(value: f64) { println!("{value:.17e}"); }

fn angle(case: usize) -> f64 {
    match case {
        2 | 3 => -FRAC_PI_2,
        4 | 5 => 3.0 * FRAC_PI_2,
        6 | 7 => TAU,
        8 | 9 => 1.99 * PI,
        _ => FRAC_PI_2,
    }
}

fn profile(case: usize) -> ([f64; 2], [f64; 2]) {
    match case {
        10 | 11 => ([2.0, 0.0], [4.0, 5.0]),
        16 => ([4.0, 0.0], [0.0, 5.0]),
        17 | 18 => ([2.0, 5.0], [4.0, 0.0]),
        _ => ([4.0, 0.0], [2.0, 5.0]),
    }
}

fn output(body: Body) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
        body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
        body.coedges.iter().filter(|(_, use_node)| use_node.pcurve.is_some()).count(),
        body.faces.iter().filter(|(_, face)| face.forward).count(),
    ] { emit(count as f64); }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
}

fn cone_parameters(body: &Body) -> Vec<(f64, f64)> {
    body.surfaces.iter().filter_map(|(_, surface)| match surface {
        Surface::Cone(cone) => Some((cone.radius, cone.half_angle)),
        _ => None,
    }).collect()
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let (first, last) = profile(case);
    let sheet = if case == 15 { make::cone([0.0; 3], 4.0, 5.0) } else {
        let section = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
        revolve_surface(section, &[Curve::Line(Line { start: first, end: last })],
                        [0.0; 3], [0.0, 0.0, 1.0], angle(case))
    }.expect("conical source");
    let original = (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                    sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len());
    let original_points = sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    let original_cones = cone_parameters(&sheet);
    let face = sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap();
    let forward = face.forward;
    let distance = match case {
        0..=11 => if case % 2 == 0 { 0.5 } else { -0.5 },
        12 => 0.0,
        13 => if forward { -5.0 } else { 5.0 },
        14 => f64::INFINITY,
        15 | 16 | 17 => 0.5,
        18 => -0.5,
        _ => panic!("unknown case"),
    };
    let result = thicken(&sheet, distance);
    let unchanged = original == (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                                 sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len())
        && sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap().forward == forward
        && cone_parameters(&sheet) == original_cones
        && sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>() == original_points;
    emit(result.is_ok() as u8 as f64);
    emit(unchanged as u8 as f64);
    emit(forward as u8 as f64);
    if let Ok(body) = result { output(body); }
}
