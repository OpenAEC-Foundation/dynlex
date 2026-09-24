// SPDX-License-Identifier: MPL-2.0
// Geometry and topology oracle from the pinned cadkernel revision.
use cadkernel::brep::{planar_face_profile, planar_region};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

fn rectangle(x0: f64, y0: f64, x1: f64, y1: f64, clockwise: bool) -> Vec<Curve> {
    let mut points = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    if clockwise {
        points.reverse();
    }
    (0..4).map(|index| Curve::Line(Line {
        start: points[index],
        end: points[(index + 1) % 4],
    })).collect()
}

fn case_loops(case: usize) -> Vec<Vec<Curve>> {
    let clockwise_outer = matches!(case, 1 | 3 | 4);
    let mut loops = vec![rectangle(0.0, 0.0, 10.0, 10.0, clockwise_outer)];
    match case {
        0 | 1 => {}
        2 | 4 => loops.push(rectangle(3.0, 3.0, 5.0, 5.0, false)),
        3 => loops.push(rectangle(3.0, 3.0, 5.0, 5.0, true)),
        5 => loops.push(rectangle(12.0, 2.0, 14.0, 4.0, false)),
        6 => loops.push(rectangle(8.0, 2.0, 10.0, 4.0, false)),
        7 => loops.push(rectangle(9.0, 2.0, 11.0, 4.0, false)),
        8 => {
            loops.push(rectangle(2.0, 2.0, 8.0, 8.0, false));
            loops.push(rectangle(4.0, 4.0, 6.0, 6.0, false));
        }
        9 => {
            loops.push(rectangle(2.0, 2.0, 5.0, 5.0, false));
            loops.push(rectangle(4.0, 4.0, 7.0, 7.0, false));
        }
        10 => {
            loops.push(rectangle(2.0, 2.0, 3.0, 3.0, false));
            loops.push(rectangle(6.0, 6.0, 7.0, 7.0, false));
        }
        _ => panic!("unknown case"),
    }
    loops
}

fn main() {
    let case = std::env::args().nth(1).expect("case").parse().expect("integer case");
    let Some(body) = planar_region(Plane::XY, &case_loops(case)) else {
        println!("0");
        return;
    };
    let face = body.face_keys().next().expect("face");
    let profile = planar_face_profile(&body, face).expect("planar face");
    println!("1 {} {} {}", body.validate().len(), body.faces.get(face).unwrap().forward as u8, profile.loops.len());
    for ring in profile.loops {
        let signed_area: f64 = ring.iter().map(Curve::enclosed_area).sum();
        println!("{signed_area:.17e}");
    }
}
