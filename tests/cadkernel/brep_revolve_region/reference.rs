// SPDX-License-Identifier: MPL-2.0
// Pinned source: cadkernel src/brep/sweep.rs at
// 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::revolve_region;
use cadkernel::geom2d::{Arc, Curve, Line};
use cadkernel::space::Plane;

fn rectangle(x0: f64, y0: f64, x1: f64, y1: f64) -> Vec<Curve> {
    let points = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    (0..4).map(|index| Curve::Line(Line {
        start: points[index], end: points[(index + 1) % 4],
    })).collect()
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("numeric case");
    let outer = rectangle(2.0, 0.0, 5.0, 4.0);
    let hole = rectangle(3.0, 1.0, 4.0, 3.0);
    let second_hole = rectangle(2.2, 1.0, 2.6, 2.0);
    let distant_hole = rectangle(7.0, 1.0, 8.0, 3.0);
    let crossing = rectangle(-1.0, 0.0, 5.0, 4.0);
    let open = vec![Curve::Line(Line { start: [2.0, 0.0], end: [5.0, 0.0] })];
    let arc_profile = |centre: [f64; 2], radius: f64| vec![
        Curve::Arc(Arc { centre, radius, start_angle: -std::f64::consts::FRAC_PI_2,
                         end_angle: std::f64::consts::FRAC_PI_2 }),
        Curve::Line(Line { start: [centre[0], radius], end: centre }),
        Curve::Line(Line { start: centre, end: [centre[0], -radius] }),
    ];
    let (profiles, angle): (Vec<Vec<Curve>>, f64) = match case {
        0 => (vec![outer.clone()], std::f64::consts::TAU),
        1 => (vec![outer.clone(), hole.clone()], std::f64::consts::TAU),
        2 => (vec![outer.clone(), hole.clone()], std::f64::consts::PI),
        3 => (vec![outer.clone(), distant_hole], std::f64::consts::TAU),
        4 => (vec![outer.clone(), hole.clone()], -std::f64::consts::PI),
        5 => (vec![], std::f64::consts::TAU),
        6 => (vec![outer.clone(), open], std::f64::consts::TAU),
        7 => (vec![crossing], std::f64::consts::TAU),
        8 => (vec![outer, hole], 0.0),
        9 => (vec![rectangle(0.0, 0.0, 3.0, 4.0)], std::f64::consts::FRAC_PI_2),
        10 => (vec![arc_profile([10.0, 0.0], 2.0)], std::f64::consts::FRAC_PI_2),
        11 => (vec![arc_profile([0.0, 0.0], 4.0)], std::f64::consts::FRAC_PI_2),
        12 => (vec![outer.clone(), distant_hole], std::f64::consts::PI),
        13 => (vec![outer.clone(), hole.clone(), second_hole.clone()], std::f64::consts::TAU),
        14 => (vec![outer, hole, second_hole], std::f64::consts::PI),
        _ => panic!("unsupported case"),
    };
    let plane = Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]);
    let result = revolve_region(plane, &profiles, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0], angle);
    println!("{}", result.is_some() as u8);
    if let Some(body) = result {
        for count in [body.vertices.len(), body.edges.len(), body.coedges.len(),
                      body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
                      body.surfaces.len(), body.curves.len(), body.roots.len(), body.validate().len()] {
            println!("{count}");
        }
        println!("{}", body.coedges.iter().filter(|(_, coedge)| coedge.pcurve.is_some()).count());
        for root in &body.roots {
            println!("{}", body.lumps.get(*root).unwrap().shells.len());
        }
        for (_, vertex) in body.vertices.iter() {
            for coordinate in vertex.point {
                println!("{coordinate:.17e}");
            }
        }
    }
}
