// SPDX-License-Identifier: MPL-2.0
// Uses the unmodified pinned modules selected by verify.py.
use space::{arclength::ArcLengthCurve3, NurbsCurve3};
fn scalar(value: f64) { println!("{value:.17e}"); }
fn point(value: [f64; 3]) { for coordinate in value { scalar(coordinate); } }
fn main() {
    let input: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let kind = input[0] as usize;
    let degree = input[1] as usize;
    let count = input[2] as usize;
    let knot_count = input[3] as usize;
    let weight_count = input[4] as usize;
    let closed = input[5] != 0.0;
    let parameter = input[6];
    let distance = input[7];
    let mut cursor = 8;
    let points: Vec<[f64; 3]> = (0..count).map(|_| {
        let p = [input[cursor], input[cursor + 1], input[cursor + 2]];
        cursor += 3; p
    }).collect();
    let knots = input[cursor..cursor + knot_count].to_vec(); cursor += knot_count;
    let weights = input[cursor..cursor + weight_count].to_vec();
    let result = if kind == 0 {
        ArcLengthCurve3::from_polyline(&points, closed)
    } else {
        let curve = if kind == 1 { NurbsCurve3::new_strict(degree, points, knots, weights) }
                    else { NurbsCurve3::new(degree, points, knots, Some(weights)) };
        curve.and_then(|c| ArcLengthCurve3::from_nurbs(c.with_periodicity(closed)))
    };
    scalar(if result.is_some() { 1.0 } else { 0.0 });
    if let Some(curve) = result {
        scalar(curve.length());
        scalar(if curve.is_closed() {1.0} else {0.0});
        point(curve.point_at(parameter));
        point(curve.tangent_at(parameter));
        scalar(curve.parameter_at_distance(distance));
        point(curve.point_at_distance(distance));
        let copied = curve.clone();
        drop(curve);
        scalar(copied.length());
        point(copied.point_at_distance(distance));
    }
}
