// SPDX-License-Identifier: MPL-2.0
// Pinned reference for the public base and placement helpers.
use cadkernel::brep::{sweep_path_start, sweep_profile_base, sweep_profile_group_base, sweep_profile_placement, Placement, SweepOptions, SweepPath};
use cadkernel::geom2d::{Arc, Circle, Curve, Ellipse, EllipseArc, Line, NurbsCurve, Polyline, PolylineVertex, Ray};
use cadkernel::space::{NurbsCurve3, Plane};

fn number(value: f64) { println!("{value:.17e}"); }
fn point(value: Option<[f64; 3]>) {
    number(value.is_some() as u8 as f64);
    for coordinate in value.unwrap_or([0.0; 3]) { number(coordinate); }
}
fn placement(value: Option<Placement>) {
    number(value.is_some() as u8 as f64);
    let value = value.unwrap_or(Placement::IDENTITY);
    for axis in [value.x_axis, value.y_axis, value.z_axis, value.origin] {
        for coordinate in axis { number(coordinate); }
    }
}
fn line(start: [f64; 2], end: [f64; 2]) -> Curve { Curve::Line(Line { start, end }) }
fn main() {
    let xy = Plane::XY;
    let wire = vec![line([0.0, 0.0], [2.0, 0.0])];
    let wires = vec![wire.clone()];
    point(sweep_profile_base(xy, &wires));
    point(sweep_profile_base(xy, &[vec![
        line([0.0, 0.0], [2.0, 0.0]), line([2.0, 2.0], [2.0, 0.0]),
    ]]));
    point(sweep_profile_base(xy, &[vec![Curve::Circle(Circle {
        centre: [8.0, -3.0], radius: 2.0,
    })]]));
    point(sweep_profile_base(xy, &[vec![Curve::Arc(Arc {
        centre: [0.0, 0.0], radius: 2.0, start_angle: 0.0,
        end_angle: std::f64::consts::FRAC_PI_2,
    })]]));
    let long = vec![line([10.0, 0.0], [10.0, 4.0])];
    point(sweep_profile_base(xy, &[wire.clone(), long]));
    let elevated = Plane::from_axes([0.0, 0.0, 3.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    point(sweep_profile_group_base(&[
        (xy, wires.clone()),
        (elevated, vec![vec![line([10.0, 0.0], [14.0, 0.0])]]),
    ]));
    point(sweep_profile_base(xy, &[vec![Curve::Ellipse(EllipseArc::full(Ellipse {
        centre: [3.0, -5.0], major_radius: 2.0, minor_radius: 1.0,
        major_axis: [1.0, 0.0],
    }))]]));
    point(sweep_profile_base(xy, &[vec![Curve::Polyline(Polyline {
        vertices: vec![
            PolylineVertex::curved([0.0, 0.0], -1.0),
            PolylineVertex::straight([2.0, 0.0]),
        ],
        closed: false,
    })]]));
    let scaled = Plane::from_axes([1.0, 2.0, 3.0], [2.0, 0.0, 0.0], [0.0, 3.0, 0.0]);
    point(sweep_profile_base(scaled, &[vec![Curve::Arc(Arc {
        centre: [0.0, 0.0], radius: 2.0, start_angle: 0.0,
        end_angle: std::f64::consts::FRAC_PI_2,
    })]]));
    let profile_spline = NurbsCurve::new_strict(2,
        vec![[0.0, 0.0], [1.0, 1.0], [2.0, 0.0]],
        vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], vec![1.0; 3]).unwrap();
    point(sweep_profile_base(xy, &[vec![Curve::Nurbs(profile_spline)]]));
    point(sweep_profile_base(xy, &[vec![Curve::Ray(Ray {
        origin: [0.0, 0.0], direction: [1.0, 0.0],
    })]]));
    point(sweep_profile_base(xy, &[]));

    let path_plane = Plane::from_axes([0.0, 0.0, 5.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let path = vec![line([4.0, 0.0], [4.0, 3.0])];
    point(sweep_path_start(SweepPath::Planar { plane: path_plane, curves: &path }));
    let points = [[7.0, 8.0, 9.0], [8.0, 8.0, 9.0]];
    point(sweep_path_start(SweepPath::Polyline3d { points: &points, closed: false }));
    let still = [[0.0; 3], [0.0; 3]];
    point(sweep_path_start(SweepPath::Polyline3d { points: &still, closed: false }));
    let reversed = vec![line([0.0, 0.0], [2.0, 0.0]), line([4.0, 0.0], [2.0, 0.0])];
    point(sweep_path_start(SweepPath::Planar { plane: xy, curves: &reversed }));
    let spline = NurbsCurve3::new_strict(1, vec![[3.0, 4.0, 5.0], [5.0, 4.0, 5.0]],
        vec![0.0, 0.0, 1.0, 1.0], vec![1.0, 1.0]).unwrap();
    point(sweep_path_start(SweepPath::Nurbs3(&spline)));
    let disconnected = vec![line([0.0, 0.0], [1.0, 0.0]), line([3.0, 0.0], [4.0, 0.0])];
    point(sweep_path_start(SweepPath::Planar { plane: xy, curves: &disconnected }));
    let curved_path = vec![Curve::Arc(Arc {
        centre: [4.0, 0.0], radius: 3.0, start_angle: 0.0,
        end_angle: std::f64::consts::FRAC_PI_2,
    })];
    point(sweep_path_start(SweepPath::Planar { plane: path_plane, curves: &curved_path }));

    placement(sweep_profile_placement(xy, &wires,
        SweepPath::Planar { plane: path_plane, curves: &path }, SweepOptions::default()));
    placement(sweep_profile_placement(xy, &wires,
        SweepPath::Planar { plane: path_plane, curves: &path },
        SweepOptions { align: false, rotation: std::f64::consts::FRAC_PI_2, ..SweepOptions::default() }));
    placement(sweep_profile_placement(xy, &wires,
        SweepPath::Polyline3d { points: &points, closed: false }, SweepOptions::default()));
    let backward = [[7.0, 8.0, 9.0], [6.0, 8.0, 9.0]];
    placement(sweep_profile_placement(xy, &wires,
        SweepPath::Polyline3d { points: &backward, closed: false }, SweepOptions::default()));
    placement(sweep_profile_placement(xy, &[],
        SweepPath::Planar { plane: path_plane, curves: &path },
        SweepOptions { base_point: Some([10.0, 20.0, 30.0]), ..SweepOptions::default() }));
    placement(sweep_profile_placement(xy, &wires, SweepPath::Nurbs3(&spline),
        SweepOptions::default()));
    placement(sweep_profile_placement(xy, &wires,
        SweepPath::Planar { plane: path_plane, curves: &path },
        SweepOptions { twist: 0.5, scale: 2.0, bank: true, surface: true,
            ..SweepOptions::default() }));
}
