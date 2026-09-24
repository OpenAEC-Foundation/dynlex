// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{make, revolve_surface, thicken, Body, Surface};
use cadkernel::geom2d::{Arc, Curve};
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

fn profile(case: usize) -> (f64, f64, f64, f64) {
    match case {
        10 | 11 => (5.0, 1.0, -2.5, 2.5),
        15 => (1.0, 2.0, -0.8, 0.9),
        18 | 19 => (5.0, 1.0, 0.9, -0.8),
        _ => (5.0, 1.0, -0.8, 0.9),
    }
}

fn torus_parameters(body: &Body) -> Vec<(f64, f64)> {
    body.surfaces.iter().filter_map(|(_, surface)| match surface {
        Surface::Torus(torus) => Some((torus.major_radius, torus.minor_radius)),
        _ => None,
    }).collect()
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
    let mut torii = body.face_keys().filter_map(|key| {
        let face = body.faces.get(key)?;
        match body.surfaces.get(face.surface)? {
            Surface::Torus(torus) => Some((torus.major_radius, torus.minor_radius)),
            _ => None,
        }
    }).collect::<Vec<_>>();
    torii.sort_by(|a, b| a.1.total_cmp(&b.1));
    torii.dedup_by(|a, b| (a.1 - b.1).abs() < 1e-10);
    assert_eq!(torii.len(), 2, "two toroidal boundary radii");
    assert!((torii[0].0 - torii[1].0).abs() < 1e-10, "shared major radius");
    emit(torii[0].0);
    emit(torii[0].1);
    emit(torii[1].1);
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let (major, minor, v0, v1) = profile(case);
    let sheet = if case == 17 { make::torus([0.0; 3], major, minor) } else {
        let section = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
        revolve_surface(section, &[Curve::Arc(Arc {
            centre: [major, 0.0], radius: minor, start_angle: v0, end_angle: v1,
        })], [0.0; 3], [0.0, 0.0, 1.0], angle(case))
    }.expect("toroidal source");
    let original = (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                    sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len());
    let original_points = sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    let original_torii = torus_parameters(&sheet);
    let face = sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap();
    let forward = face.forward;
    let distance = match case {
        0..=11 => if case % 2 == 0 { 0.25 } else { -0.25 },
        12 => 0.0,
        13 => if forward { -2.0 } else { 2.0 },
        14 => if forward { 5.0 } else { -5.0 },
        15 => 0.25,
        16 => f64::INFINITY,
        17 | 18 => 0.25,
        19 => -0.25,
        _ => panic!("unknown case"),
    };
    let result = thicken(&sheet, distance);
    let unchanged = original == (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                                 sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len())
        && sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap().forward == forward
        && torus_parameters(&sheet) == original_torii
        && sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>() == original_points;
    emit(result.is_ok() as u8 as f64);
    emit(unchanged as u8 as f64);
    emit(forward as u8 as f64);
    if let Ok(body) = result { output(body); }
}
