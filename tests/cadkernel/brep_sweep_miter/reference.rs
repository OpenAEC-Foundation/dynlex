// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/sweep.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::sweep_along;
use cadkernel::geom2d::{Curve as Curve2, Line};
use cadkernel::space::Plane;

fn line(start: [f64; 2], end: [f64; 2]) -> Curve2 {
    Curve2::Line(Line { start, end })
}

fn emit(value: f64) {
    println!("{value:.17e}");
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let profile_plane = Plane::from_axes([0.0; 3], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]);
    let path_plane = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let profile = [
        line([-1.0, 0.0], [1.0, 0.0]),
        line([1.0, 0.0], [1.0, 3.0]),
        line([1.0, 3.0], [-1.0, 3.0]),
        line([-1.0, 3.0], [-1.0, 0.0]),
    ];
    let path = match case {
        0 => vec![line([0.0, 0.0], [5.0, 0.0]), line([5.0, 0.0], [5.0, 5.0])],
        1 => vec![line([0.0, 0.0], [5.0, 0.0]), line([5.0, 0.0], [8.0, 4.0])],
        2 => vec![line([0.0, 0.0], [5.0, 0.0]), line([5.0, 0.0], [8.0, 0.0])],
        3 => vec![line([5.0, 0.0], [0.0, 0.0]), line([5.0, 5.0], [5.0, 0.0])],
        4 => vec![line([0.0, 0.0], [5.0, 0.0])],
        5 => vec![
            line([0.0, 0.0], [5.0, 0.0]),
            line([5.0, 0.0], [5.0, 5.0]),
            line([5.0, 5.0], [8.0, 5.0]),
        ],
        _ => panic!("unsupported case"),
    };
    let result = sweep_along(profile_plane, &profile, path_plane, &path);
    emit(result.is_some() as u8 as f64);
    if let Some(body) = result {
        for count in [
            body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
            body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
            body.curves.len(), body.roots.len(), body.validate().len(),
            body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
            body.coedges.iter().filter(|(_, coedge)| coedge.pcurve.is_some()).count(),
            body.faces.iter().filter(|(_, face)| face.forward).count(),
        ] {
            emit(count as f64);
        }
        for (_, vertex) in body.vertices.iter() {
            for coordinate in vertex.point {
                emit(coordinate);
            }
        }
    }
}
