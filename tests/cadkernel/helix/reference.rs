// SPDX-License-Identifier: MPL-2.0
// The enclosing driver includes whole, unmodified modules from the pinned source.
use space::{helix::{HelixCurve, HelixDirection}, NurbsCurve3};
fn scalar(x: f64) { println!("{x:.17e}"); }
fn flag(x: bool) { scalar(if x { 1.0 } else { 0.0 }); }
fn point(p: [f64; 3]) { println!("p {:.17e} {:.17e} {:.17e}", p[0], p[1], p[2]); }
fn option(x: Option<f64>) { flag(x.is_some()); if let Some(x) = x { scalar(x); } }
fn triple(a: &[f64], i: usize) -> [f64; 3] { a[i..i+3].try_into().unwrap() }
fn parameters(c: &HelixCurve) {
    point(c.base_center); point(c.axis_direction); point(c.start_direction);
    scalar(c.base_radius); scalar(c.top_radius); scalar(c.height); scalar(c.turns);
    flag(c.direction == HelixDirection::Clockwise);
}
fn curve(c: &NurbsCurve3) {
    scalar(c.degree() as f64); scalar(c.control_points().len() as f64);
    scalar(c.knots().len() as f64); scalar(c.weights().len() as f64);
    flag(c.periodicity());
    let n = c.control_points().len();
    for i in 0..n { if n <= 128 || i < 4 || i >= n-4 || i == n/2 { point(c.control_points()[i]); } }
    let n = c.knots().len();
    for i in 0..n { if n <= 128 || i < 4 || i >= n-4 || i == n/2 { scalar(c.knots()[i]); } }
    let n = c.weights().len();
    for i in 0..n { if n <= 128 || i < 4 || i >= n-4 || i == n/2 { scalar(c.weights()[i]); } }
    for t in [-0.25, -0.0, 0.0, 0.01, 0.125, 0.37, 0.5, 0.83, 0.999, 1.0, 1.25] {
        point(c.point_at(t)); point(c.tangent_at(t));
    }
}
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let a: Vec<f64> = args.iter().map(|x| x.parse().unwrap()).collect();
    if a[0] == 2.0 {
        assert_eq!(a.len(), 2);
        scalar(a[1].sin()); scalar(a[1].cos());
    } else if a[0] == 1.0 {
        assert_eq!(a.len(), 13);
        let result = HelixCurve::frame_from_points(triple(&a,1),triple(&a,4),triple(&a,7),triple(&a,10));
        flag(result.is_some());
        if let Some((axis, start, radius)) = result { point(axis); point(start); scalar(radius); }
    } else {
        assert_eq!(a.len(), 15);
        let c = HelixCurve { base_center: triple(&a,1), axis_direction: triple(&a,4), start_direction: triple(&a,7),
            base_radius:a[10], top_radius:a[11], height:a[12], turns:a[13],
            direction:if a[14] != 0.0 {HelixDirection::Clockwise} else {HelixDirection::CounterClockwise} };
        option(c.length()); option(c.turn_slope());
        let n = c.nurbs(); flag(n.is_some()); if let Some(n) = n { curve(&n); }
        let r = c.reversed(); flag(r.is_some());
        if let Some(r) = r {
            parameters(&r); option(r.length()); option(r.turn_slope());
            let n = r.nurbs(); flag(n.is_some()); if let Some(n) = n { curve(&n); }
            let rr = r.reversed(); flag(rr.is_some()); if let Some(rr) = rr { parameters(&rr); }
        }
    }
}
