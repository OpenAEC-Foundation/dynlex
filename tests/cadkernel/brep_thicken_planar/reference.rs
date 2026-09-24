// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{extrude_region, planar_region, thicken, Body};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

fn square(x: f64, y: f64, size: f64) -> Vec<Curve> {
    let points = [[x, y], [x + size, y], [x + size, y + size], [x, y + size]];
    (0..4).map(|index| Curve::Line(Line {
        start: points[index], end: points[(index + 1) % 4],
    })).collect()
}

fn emit(value: f64) { println!("{value:.17e}"); }

fn output(body: Body) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
        body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
        body.coedges.iter().filter(|(_, coedge)| coedge.pcurve.is_some()).count(),
        body.faces.iter().filter(|(_, face)| face.forward).count(),
    ] { emit(count as f64); }
    for (_, vertex) in body.vertices.iter() {
        for value in vertex.point { emit(value); }
    }
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("integer case");
    let outer = square(0.0, 0.0, 10.0);
    let mut profiles = vec![outer];
    if case == 2 || case == 3 {
        profiles.push(square(3.0, 3.0, 2.0));
    }
    let source = if case == 5 {
        extrude_region(Plane::XY, &profiles, [0.0, 0.0, 3.0])
    } else {
        planar_region(Plane::XY, &profiles)
    }.expect("source body");
    let distance = match case {
        0 | 2 | 5 => 2.0,
        1 | 3 => -2.0,
        4 => 0.0,
        _ => panic!("unsupported case"),
    };
    let result = thicken(&source, distance);
    emit(result.is_ok() as u8 as f64);
    if let Ok(body) = result { output(body); }
}
