// SPDX-License-Identifier: MPL-2.0
// Compare public B-rep output from the pinned Rust implementation.
use cadkernel::brep::{revolve_surface, Surface};
use cadkernel::geom2d::{Curve as Curve2, Ellipse, EllipseArc, NurbsCurve};
use cadkernel::space::Plane;

fn emit(value: f64) {
    println!("{value:.17e}");
}

fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|arg| arg.parse().unwrap()).collect();
    let kind = values[0] as u8;
    let angle = values[1];
    let piece = match kind {
        3 => Curve2::Ellipse(EllipseArc {
            ellipse: Ellipse {
                centre: [values[2], values[3]],
                major_radius: values[4],
                minor_radius: values[5],
                major_axis: [values[6], values[7]],
            },
            start_parameter: values[8],
            end_parameter: values[9],
        }),
        5 => Curve2::Nurbs(NurbsCurve::new_strict(
            2,
            vec![[values[2], values[3]], [values[4], values[5]], [values[6], values[7]]],
            values[11..17].to_vec(),
            values[8..11].to_vec(),
        ).unwrap()),
        _ => panic!("unknown curve kind"),
    };
    let result = revolve_surface(Plane::XY, &[piece], [0.0; 3], [0.0, 1.0, 0.0], angle);
    let Some(body) = result else { emit(0.0); return; };
    let surface = body.surfaces.iter().find_map(|(_, shape)| match shape {
        Surface::Nurbs(spline) => Some(spline),
        _ => None,
    }).expect("rational profile surface");
    emit(1.0);
    let (u_degree, v_degree) = surface.degrees();
    emit(u_degree as f64);
    emit(v_degree as f64);
    let net = surface.control_points();
    emit(net.len() as f64);
    emit(net[0].len() as f64);
    let periodic = surface.periodicity();
    emit(periodic[0] as u8 as f64);
    emit(periodic[1] as u8 as f64);
    let (u_knots, v_knots) = surface.knots();
    emit(u_knots.len() as f64);
    emit(v_knots.len() as f64);
    for knot in u_knots { emit(*knot); }
    for knot in v_knots { emit(*knot); }
    for (row, weights) in net.iter().zip(surface.weights()) {
        for (point, weight) in row.iter().zip(weights) {
            for coordinate in point { emit(*coordinate); }
            emit(*weight);
        }
    }
}
