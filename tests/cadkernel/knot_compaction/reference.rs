// SPDX-License-Identifier: MPL-2.0
// Driver for unmodified modules at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use space::NurbsCurve3;

fn emit(x: f64) { println!("{x:.17e}"); }
fn flag(x: bool) { emit(if x { 1.0 } else { 0.0 }); }
fn point(p: [f64; 3]) { for x in p { emit(x); } }
fn curve(c: &NurbsCurve3) {
    emit(c.degree() as f64);
    emit(c.control_points().len() as f64);
    emit(c.knots().len() as f64);
    emit(c.weights().len() as f64);
    flag(c.periodicity());
    flag(c.is_rational());
    for p in c.control_points() { point(*p); }
    for x in c.knots() { emit(*x); }
    for x in c.weights() { emit(*x); }
    let (a, b) = c.domain(); emit(a); emit(b);
}

fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|x| x.parse().unwrap()).collect();
    let degree = args[0] as usize;
    let count = args[1] as usize;
    let knot_count = args[2] as usize;
    let closed = args[3] != 0.0;
    let strict = args[4] != 0.0;
    let tolerance = args[5];
    let evaluate = args[6] != 0.0;
    let mut cursor = 7;
    let mut points = Vec::new();
    for _ in 0..count {
        points.push([args[cursor], args[cursor + 1], args[cursor + 2]]);
        cursor += 3;
    }
    let knots = args[cursor..cursor + knot_count].to_vec(); cursor += knot_count;
    let weights = args[cursor..cursor + count].to_vec();
    let constructed = if strict {
        NurbsCurve3::new_strict(degree, points, knots, weights)
    } else {
        NurbsCurve3::new(degree, points, knots, Some(weights))
    };
    flag(constructed.is_some());
    if let Some(c) = constructed {
        let shape = c.with_periodicity(closed);
        let compacted = shape.compact_knots(tolerance);
        flag(compacted.is_some());
        if let Some(result) = compacted {
            curve(&result);
            if evaluate {
                for step in 0..=20 { point(result.point_at(step as f64 / 20.0)); }
            }
            flag(shape.control_points().as_ptr() != result.control_points().as_ptr());
            flag(shape.knots().as_ptr() != result.knots().as_ptr());
            flag(shape.weights().as_ptr() != result.weights().as_ptr());
            // Rust's owned Vec clones are independent; DynLex also checks its owner.
            flag(true);
        }
    }
}
