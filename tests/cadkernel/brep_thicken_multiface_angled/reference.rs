// SPDX-License-Identifier: MPL-2.0
// Angled connected planar pair from pinned cadkernel src/brep/thicken.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{extrude_surface, make, thicken, Body, Surface};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

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
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
}

fn planar_surfaces(body: &Body) -> Vec<Plane> {
    body.surfaces.iter().filter_map(|(_, surface)| match surface {
        Surface::Plane(plane) => Some(*plane),
        _ => None,
    }).collect()
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let sheet = if case == 8 {
        make::cuboid([0.0; 3], [4.0, 1.0, 3.0])
    } else {
        let points = match case {
            2 | 3 => [[0.0, 0.0], [2.0, 0.0], [3.0, 2.0]],
            4 | 5 => [[0.0, 0.0], [2.0, 0.0], [2.0, -2.0]],
            _ => [[0.0, 0.0], [2.0, 0.0], [2.0, 2.0]],
        };
        let profile = [
            Curve::Line(Line { start: points[0], end: points[1] }),
            Curve::Line(Line { start: points[1], end: points[2] }),
        ];
        extrude_surface(Plane::XY, &profile, [0.0, 0.0, 3.0])
    }.expect("connected planar source");
    if case != 8 { assert_eq!(sheet.faces.len(), 2, "two source faces"); }
    let original = (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                    sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len());
    let original_points = sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    let original_planes = planar_surfaces(&sheet);
    let original_forward = sheet.face_keys().map(|key| sheet.faces.get(key).unwrap().forward).collect::<Vec<_>>();
    let distance = match case {
        0 | 2 | 4 => 0.25,
        1 | 3 | 5 => -0.25,
        6 => 0.0,
        7 => f64::INFINITY,
        8 => 0.25,
        _ => panic!("unknown case"),
    };
    let result = thicken(&sheet, distance);
    let unchanged = original == (sheet.faces.len(), sheet.edges.len(), sheet.coedges.len(), sheet.loops.len(),
                                 sheet.vertices.len(), sheet.surfaces.len(), sheet.curves.len(), sheet.roots.len())
        && sheet.face_keys().map(|key| sheet.faces.get(key).unwrap().forward).collect::<Vec<_>>() == original_forward
        && planar_surfaces(&sheet) == original_planes
        && sheet.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>() == original_points;
    emit(result.is_ok() as u8 as f64);
    emit(unchanged as u8 as f64);
    if let Ok(body) = result { output(body); }
}
