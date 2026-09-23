// SPDX-License-Identifier: MPL-2.0
// Test wire decoding and output only; operations are the unchanged source.
mod input {
    use crate::geom2d;
    include!("../cross2/reference.rs");
    pub fn extend_scale(by: &geom2d::transform::Transform) {
        let extent = f64::from_bits(SCALE.load(Ordering::Relaxed));
        let x = by.x_axis.x.abs().max(by.x_axis.y.abs());
        let y = by.y_axis.x.abs().max(by.y_axis.y.abs());
        record_scale([extent * (x + y), by.origin.x, by.origin.y, x, y]);
    }
}
use input::{shape, number, integer, point};
use geom2d::{curve::Curve, transform::Transform, Vec2};

fn emit_curve(value: &Curve) {
    match value {
        Curve::Line(x) => { integer(0); point(x.start); point(x.end); }
        Curve::Circle(x) => { integer(1); point(x.centre); number(x.radius); }
        Curve::Arc(x) => { integer(2); point(x.centre); number(x.radius); number(x.start_angle); number(x.end_angle); }
        Curve::Ellipse(x) => {
            integer(3); point(x.ellipse.centre); number(x.ellipse.major_radius); number(x.ellipse.minor_radius);
            point(x.ellipse.major_axis); number(x.start_parameter); number(x.end_parameter);
        }
        Curve::Polyline(x) => {
            integer(4); integer(usize::from(x.closed)); integer(x.vertices.len());
            for v in &x.vertices { point(v.position); number(v.bulge); }
        }
        Curve::Nurbs(x) => {
            integer(5); integer(x.degree()); integer(x.control_points().len());
            for p in x.control_points() { point(*p); }
            integer(x.knots().len()); for t in x.knots() { number(*t); }
            integer(x.weights().len()); for w in x.weights() { number(*w); }
        }
        Curve::Ray(x) => { integer(6); point(x.origin); point(x.direction); }
        Curve::XLine(x) => { integer(7); point(x.base); point(x.direction); }
    }
}
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    let by = Transform { x_axis: Vec2::new(args[0],args[1]), y_axis: Vec2::new(args[2],args[3]), origin: Vec2::new(args[4],args[5]) };
    let shape = shape(&args[7..]);
    let original = shape.clone();
    input::extend_scale(&by);
    number(by.determinant()); integer(usize::from(by.is_similarity()));
    point(by.apply_point([args[9],args[10]])); point(by.apply_vector([args[9],args[10]]));
    let result = shape.transformed(&by);
    integer(usize::from(result.is_some()));
    if let Some(value) = result {
        emit_curve(&value);
        for t in [-0.25,0.0,0.37,1.0,1.25] { point(value.point_at(t)); point(value.tangent_at(t)); }
    }
    integer(usize::from(shape == original));
}
