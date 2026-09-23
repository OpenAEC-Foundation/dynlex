// SPDX-License-Identifier: MPL-2.0
// Pinned source: cadkernel src/brep/sweep.rs at
// 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::sweep_along;
use cadkernel::geom2d::{Arc, Curve, Line};
use cadkernel::space::Plane;

fn line(start: [f64; 2], end: [f64; 2]) -> Curve {
    Curve::Line(Line { start, end })
}

fn triangle(points: [[f64; 2]; 3]) -> Vec<Curve> {
    (0..3).map(|index| line(points[index], points[(index + 1) % 3])).collect()
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("numeric case");
    let path_plane = Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let line_frame = Plane::from_axes([0.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]);
    let arc_frame = Plane::from_axes([1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [1.0, 0.0, 0.0]);
    let late_arc_frame = Plane::from_axes([0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]);
    let mixed_frame = Plane::from_axes([1.0, -1.0, 0.0], [0.0, 0.0, 1.0], [1.0, 0.0, 0.0]);
    let straight_profile = triangle([[0.0, 0.0], [1.0, 0.0], [0.5, 1.0]]);
    let curved_profile = triangle([[0.0, 0.0], [0.0, 1.0], [1.0, 0.5]]);
    let quarter = Curve::Arc(Arc { centre: [0.0, 0.0], radius: 1.0,
                                   start_angle: 0.0, end_angle: std::f64::consts::FRAC_PI_2 });
    let late = Curve::Arc(Arc { centre: [0.0, 0.0], radius: 1.0,
                                start_angle: std::f64::consts::FRAC_PI_2,
                                end_angle: std::f64::consts::PI });
    let (plane, profile, path): (Plane, &[Curve], Vec<Curve>) = match case {
        0 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [2.0, 0.0])]),
        1 => (line_frame, &straight_profile, vec![line([2.0, 0.0], [0.0, 0.0])]),
        2 => (arc_frame, &curved_profile, vec![quarter.clone()]),
        3 => (late_arc_frame, &curved_profile, vec![late]),
        4 => (mixed_frame, &curved_profile, vec![line([1.0, -1.0], [1.0, 0.0]), quarter]),
        5 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [2.0, 0.0]), line([3.0, 0.0], [4.0, 0.0])]),
        6 => (line_frame, &straight_profile[..1], vec![line([0.0, 0.0], [2.0, 0.0])]),
        7 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [0.0, 0.0])]),
        8 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [1.0, 0.0]), line([1.0, 0.0], [2.0, 0.0])]),
        9 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [1.0, 0.0]), line([1.0, 0.0], [1.0, 1.0])]),
        10 => (line_frame, &straight_profile, vec![line([0.0, 0.0], [1.0, 0.0]), line([1.0, 0.0], [0.0, 0.0])]),
        _ => panic!("unsupported case"),
    };
    let result = sweep_along(plane, profile, path_plane, &path);
    println!("{}", result.is_some() as u8);
    if let Some(body) = result {
        for count in [body.vertices.len(), body.edges.len(), body.coedges.len(),
                      body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
                      body.surfaces.len(), body.curves.len(), body.roots.len(), body.validate().len()] {
            println!("{count}");
        }
        for (_, vertex) in body.vertices.iter() {
            for coordinate in vertex.point {
                println!("{coordinate:.17e}");
            }
        }
    }
}
