// SPDX-License-Identifier: MPL-2.0
// Included by a generated driver declaring the unmodified pinned spline module.
use spline::{Parameterization, clamped_uniform_knots, de_boor_by, span_of,
    interpolate_open, interpolate_periodic};

fn emit(value: f64) { println!("{:.17e}", value); }
fn run<const N: usize>(args: &[f64]) {
    let operation = args[1] as usize;
    let degree = args[2] as usize;
    let count = args[3] as usize;
    let knot_count = args[4] as usize;
    let spacing = match args[5] as usize {
        0 => Parameterization::Uniform, 1 => Parameterization::Centripetal,
        2 => Parameterization::Chord, _ => panic!("invalid spacing"),
    };
    let start_valid = args[6] != 0.0;
    let end_valid = args[7] != 0.0;
    let parameter = args[8];
    let knots = &args[9..9+knot_count];
    let mut cursor = 9 + knot_count;
    let points: Vec<[f64; N]> = (0..count).map(|_| {
        let point = std::array::from_fn(|axis| args[cursor + axis]);
        cursor += N;
        point
    }).collect();
    let first = std::array::from_fn(|axis| args[cursor + axis]);
    cursor += N;
    let last = std::array::from_fn(|axis| args[cursor + axis]);
    assert_eq!(cursor + N, args.len());
    if operation == 0 {
        let mut indices = Vec::new();
        let value = de_boor_by(degree, knots, count, parameter, |index| {
            indices.push(index);
            points[index]
        });
        for item in value { emit(item); }
        emit(indices.len() as f64);
        for index in indices { emit(index as f64); }
        emit(if count > 0 && degree > 0 { span_of(degree, knots, count - 1, parameter) as f64 } else { -1.0 });
    } else if operation == 3 {
        let knots = clamped_uniform_knots(degree, count);
        emit(knots.len() as f64);
        for knot in knots { emit(knot); }
    } else {
        let result = if operation == 1 {
            interpolate_open(&points, start_valid.then_some(first), end_valid.then_some(last), spacing)
        } else {
            interpolate_periodic(&points, spacing)
        };
        if let Some((controls, knots)) = result {
            emit(1.0); emit(controls.len() as f64); emit(knots.len() as f64);
            for point in controls { for value in point { emit(value); } }
            for knot in knots { emit(knot); }
        } else {
            emit(0.0); emit(0.0); emit(0.0);
        }
    }
}
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|text| text.parse().unwrap()).collect();
    match args[0] as usize {
        0 => run::<0>(&args), 1 => run::<1>(&args), 2 => run::<2>(&args),
        3 => run::<3>(&args), 4 => run::<4>(&args), 5 => run::<5>(&args),
        8 => run::<8>(&args), 17 => run::<17>(&args),
        _ => panic!("unsupported test dimension"),
    }
}
