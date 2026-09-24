// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{make, revolve_surface, thicken, Body, Surface};
use cadkernel::geom2d::{Arc, Curve};
use cadkernel::space::Plane;
use std::f64::consts::{FRAC_PI_2, PI, TAU};

fn emit(value: f64) { println!("{value:.17e}"); }

fn angles(case: usize) -> (f64, f64, f64) {
    let angle = match case {
        2 | 3 => -FRAC_PI_2,
        4 | 5 => 3.0 * FRAC_PI_2,
        6 | 7 => TAU,
        8 | 9 => 1.99 * PI,
        _ => FRAC_PI_2,
    };
    if case == 10 || case == 11 { (angle, -FRAC_PI_2, 0.3) }
    else { (angle, -0.6, 0.7) }
}

fn output(body: Body, angle: f64, v0: f64, v1: f64) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
        body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
        body.coedges.iter().filter(|(_, use_node)| use_node.pcurve.is_some()).count(),
        body.faces.iter().filter(|(_, face)| face.forward).count(),
    ] { emit(count as f64); }
    let mut radii = body.face_keys().filter_map(|key| {
        let face = body.faces.get(key)?;
        match body.surfaces.get(face.surface)? {
            Surface::Sphere(sphere) => Some(sphere.radius),
            _ => None,
        }
    }).collect::<Vec<_>>();
    radii.sort_by(f64::total_cmp);
    radii.dedup_by(|a, b| (*a - *b).abs() < 1e-10);
    assert_eq!(radii.len(), 2, "two spherical boundary radii");
    let (inner, outer) = (radii[0], radii[1]);
    let radial3 = outer.powi(3) - inner.powi(3);
    let radial4 = outer.powi(4) - inner.powi(4);
    let u0 = angle.min(0.0);
    let u1 = angle.max(0.0);
    let volume = (u1 - u0) * radial3 * (v1.sin() - v0.sin()) / 3.0;
    let latitude_second = (v1 - v0) * 0.5 + ((2.0 * v1).sin() - (2.0 * v0).sin()) * 0.25;
    let radial_factor = radial4 * latitude_second / (4.0 * volume);
    let centroid = [
        radial_factor * (u1.sin() - u0.sin()),
        radial_factor * (u0.cos() - u1.cos()),
        radial4 * (v1.sin().powi(2) - v0.sin().powi(2)) * (u1 - u0) / (8.0 * volume),
    ];
    emit(volume);
    for coordinate in centroid { emit(coordinate); }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let (angle, v0, v1) = angles(case);
    let section = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
    let sheet = if case == 15 { make::sphere([0.0; 3], 4.0) } else { revolve_surface(
        section, &[Curve::Arc(Arc { centre: [0.0; 2], radius: 4.0, start_angle: v0, end_angle: v1 })],
        [0.0; 3], [0.0, 0.0, 1.0], angle,
    ) }.expect("spherical source");
    let original = (sheet.faces.len(), sheet.edges.len(), sheet.vertices.len(), sheet.surfaces.len());
    let original_points = sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    let face = sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap();
    let forward = face.forward;
    let radius = match sheet.surfaces.get(face.surface).unwrap() {
        Surface::Sphere(sphere) => sphere.radius,
        _ => panic!("sphere surface"),
    };
    let distance = match case {
        0..=11 => if case % 2 == 0 { 0.5 } else { -0.5 },
        12 => 0.0,
        13 => if face.forward { -5.0 } else { 5.0 },
        14 => f64::INFINITY,
        15 => 0.5,
        _ => panic!("unknown case"),
    };
    let result = thicken(&sheet, distance);
    let unchanged = original == (sheet.faces.len(), sheet.edges.len(), sheet.vertices.len(), sheet.surfaces.len())
        && sheet.faces.get(sheet.face_keys().next().unwrap()).unwrap().forward == forward
        && matches!(sheet.surfaces.get(face.surface), Some(Surface::Sphere(sphere)) if sphere.radius == radius)
        && sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>() == original_points;
    emit(result.is_ok() as u8 as f64);
    emit(unchanged as u8 as f64);
    if let Ok(body) = result { output(body, angle, v0, v1); }
}
