// SPDX-License-Identifier: MPL-2.0
// Pinned source: cadkernel src/brep/sweep.rs at
// 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::sweep_along_polyline3d;
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("numeric case");
    let profile = vec![
        Curve::Line(Line { start: [0.0, 0.0], end: [2.0, 0.0] }),
        Curve::Line(Line { start: [2.0, 0.0], end: [2.0, 2.0] }),
        Curve::Line(Line { start: [2.0, 2.0], end: [0.0, 2.0] }),
        Curve::Line(Line { start: [0.0, 2.0], end: [0.0, 0.0] }),
    ];
    let open = vec![Curve::Line(Line { start: [0.0, 0.0], end: [2.0, 0.0] })];
    let path: Vec<[f64; 3]> = match case {
        0 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 3.0]],
        1 => vec![[10.0, 20.0, 30.0], [10.0, 20.0, 33.0]],
        2 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 2.0], [0.0, 0.0, 4.0]],
        3 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 3.0], [2.0, 0.0, 3.0]],
        4 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 2.0], [1.0, 1.0, 3.0]],
        5 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 3.0], [0.0, 0.0, 0.0]],
        6 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 0.0]],
        7 => vec![[0.0, 0.0, 0.0]],
        8 => vec![[0.0, 0.0, 0.0], [0.0, 0.0, 3.0]],
        _ => panic!("unsupported case"),
    };
    let plane = Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let source = if case == 8 { &open } else { &profile };
    let result = sweep_along_polyline3d(plane, source, &path);
    println!("{}", result.is_some() as u8);
    if let Some(body) = result {
        for count in [
            body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
            body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
            body.curves.len(), body.roots.len(), body.validate().len(),
        ] {
            println!("{count}");
        }
        for (_, vertex) in body.vertices.iter() {
            for value in vertex.point {
                println!("{value:.17e}");
            }
        }
    }
}
