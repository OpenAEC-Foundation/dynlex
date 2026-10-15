// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{contains_point, make, Containment, Surface, Torus};
use cadkernel::space::Plane;

fn main() {
    let values: Vec<f64> = std::env::args()
        .skip(1)
        .map(|value| value.parse().expect("number"))
        .collect();
    let kind = values[0] as usize;
    let origin = [values[1], values[2], values[3]];
    let mut body = match kind {
        0 | 6 => make::cuboid(origin, [values[4], values[5], values[6]]),
        1 => make::cylinder(origin, values[4], values[5]),
        2 => make::sphere(origin, values[4]),
        3 => make::cone(origin, values[4], values[5]),
        4 => make::pyramid(origin, values[4], values[5], values[7] as usize),
        5 => make::pyramid_frustum(origin, values[4], values[6], values[5], values[7] as usize),
        _ => panic!("shape kind"),
    }
    .expect("valid body");
    if kind == 6 {
        let face = body.face_keys().next().expect("box face");
        let surface = body.faces.get(face).expect("face").surface;
        *body.surfaces.get_mut(surface).expect("surface") = Surface::Torus(Torus {
            frame: Plane::XY,
            major_radius: 10.0,
            minor_radius: 2.0,
        });
    }
    let point = [values[8], values[9], values[10]];
    let kind = match contains_point(&body, point, values[11]) {
        Containment::Inside => 0,
        Containment::Outside => 1,
        Containment::OnBoundary => 2,
        Containment::Unknown => 3,
    };
    println!("{kind}");
}
