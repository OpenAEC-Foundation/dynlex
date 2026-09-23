// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{body_bounds, make};

fn main() {
    let iterations: usize = std::env::args()
        .nth(1)
        .expect("iteration count")
        .parse()
        .expect("integer iteration count");
    assert!(iterations > 0);

    let mut discrete = 0_i64;
    let mut metric = 0.0_f64;
    for index in 0..iterations {
        let kind = index % 6;
        let height = 1.0 + (index % 17) as f64 * 0.03125;
        let radius = 0.75 + (index % 13) as f64 * 0.015625;
        let origin = [
            (index % 7) as f64 * 0.01,
            (index % 11) as f64 * -0.02,
            (index % 5) as f64 * 0.03,
        ];
        let made = match kind {
            0 => make::cuboid(origin, [radius * 2.0, radius + 0.5, height]),
            1 => make::cylinder(origin, radius, height),
            2 => make::sphere(origin, radius),
            3 => make::cone(origin, radius, height),
            4 => make::pyramid(origin, radius, height, 6),
            _ => make::pyramid_frustum(origin, radius, radius * 0.5, height, 6),
        }
        .expect("valid benchmark solid");
        let flaws = made.validate();
        discrete += made.vertices.len() as i64 * 3;
        discrete += made.edges.len() as i64 * 5;
        discrete += made.coedges.len() as i64 * 7;
        discrete += made.loops.len() as i64 * 11;
        discrete += made.faces.len() as i64 * 13;
        discrete += made.shells.len() as i64 * 17;
        discrete += made.lumps.len() as i64 * 19;
        discrete += made.surfaces.len() as i64 * 23;
        discrete += made.curves.len() as i64 * 29;
        discrete += made.euler_characteristic() * 31;
        discrete += flaws.len() as i64 * 37;
        discrete += kind as i64;
        metric += made.worst_vertex_gap();
        let bounds = body_bounds(&made).expect("bounded benchmark solid");
        metric += bounds.min[0];
        metric += bounds.min[1];
        metric += bounds.min[2];
        metric += bounds.max[0];
        metric += bounds.max[1];
        metric += bounds.max[2];
    }
    println!("{discrete} {metric:.17e}");
}
