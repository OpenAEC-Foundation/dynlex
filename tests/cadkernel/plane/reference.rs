// SPDX-License-Identifier: MPL-2.0
// The driver imports complete, unchanged modules from the pinned checkout.
use space::plane::{are_coplanar, coplanarity_tolerance, Plane};

fn emit(label: &str, index: usize, value: f64) {
    println!("{label}.{index} {value:.17e}");
}
fn array<const N: usize>(label: &str, values: [f64; N]) {
    for (index, value) in values.into_iter().enumerate() { emit(label, index, value); }
}
fn flag(label: &str, value: bool) { emit(label, 0, if value { 1.0 } else { 0.0 }); }
fn optional<const N: usize>(label: &str, value: Option<[f64; N]>) {
    flag(&format!("valid_{label}"), value.is_some());
    if let Some(value) = value { array(label, value); }
}
fn inspect(plane: Plane, uv: [f64; 2], point: [f64; 3], xy: [f64; 2], tolerance: f64) {
    array("origin", plane.origin);
    array("x_axis", plane.x_axis);
    array("y_axis", plane.y_axis);
    flag("is_orthonormal", plane.is_orthonormal());
    flag("is_xy", plane.is_xy());
    flag("is_xy_aligned", plane.is_xy_aligned());
    optional("normal", plane.normal());
    array("point", plane.point_at(uv));
    array("vector", plane.vector_at(uv));
    optional("project", plane.project(point));
    optional("project_vector", plane.project_vector(point));
    optional("coordinates_xy", plane.coordinates_at_xy(xy));
    optional("distance", plane.distance_to(point).map(|v| [v]));
    flag("contains", plane.contains(point, tolerance));
    optional("point_roundtrip", plane.project(plane.point_at(uv)));
    optional("vector_roundtrip", plane.project_vector(plane.vector_at(uv)));
}
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let triple = |index| [args[index], args[index + 1], args[index + 2]];
    match args[0] as usize {
        0 | 1 => {
            assert_eq!(args.len(), 18);
            let plane = if args[0] == 0.0 {
                Some(Plane::from_axes(triple(1), triple(4), triple(7)))
            } else { Plane::orthonormal(triple(1), triple(4), triple(7)) };
            flag("valid_frame", plane.is_some());
            if let Some(plane) = plane {
                inspect(plane, [args[10], args[11]], triple(12), [args[15], args[16]], args[17]);
            }
        }
        2 => {
            let (np, nd) = (args[1] as usize, args[2] as usize);
            assert_eq!(args.len(), 3 + 3 * (np + nd));
            let points: Vec<_> = (0..np).map(|i| triple(3 + i * 3)).collect();
            let directions: Vec<_> = (0..nd).map(|i| triple(3 + (np + i) * 3)).collect();
            emit("tolerance", 0, coplanarity_tolerance(&points));
            flag("coplanar", are_coplanar(&points, &directions));
            for (i, p) in points.iter().chain(&directions).enumerate() {
                for (axis, value) in p.iter().enumerate() { emit("input", 3 * i + axis, *value); }
            }
        }
        3 | 4 => {
            assert_eq!(args.len(), 1);
            inspect(if args[0] == 3.0 { Plane::XY } else { Plane::default() },
                [-0.0, 4.0], [4.0, -7.0, 2.0], [3.0, 5.0], 1e-9);
        }
        _ => panic!("unknown mode"),
    }
}
