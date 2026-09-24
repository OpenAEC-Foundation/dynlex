// SPDX-License-Identifier: MPL-2.0
// The differential driver replaces PINNED_SOURCE_FUNCTIONS with function bodies
// extracted verbatim from the pinned src/brep/loft_general.rs.
type H = [f64; 4];

#[derive(Clone)]
struct Bezier { control: Vec<H> }

#[derive(Clone)]
struct Span { start: f64, end: f64, curve: Bezier }

struct RationalCurve3 {
    degree: usize,
    points: Vec<[f64; 3]>,
    knots: Vec<f64>,
    weights: Vec<f64>,
}

struct LoftError(String);
fn error(message: &str) -> LoftError { LoftError(message.to_owned()) }

// PINNED_SOURCE_FUNCTIONS

fn next(args: &[String], cursor: &mut usize) -> f64 {
    let value = args[*cursor].parse::<f64>().unwrap();
    *cursor += 1;
    value
}

fn count(args: &[String], cursor: &mut usize) -> usize {
    next(args, cursor) as usize
}

fn emit(value: f64) { println!("{value:.17e}"); }

fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let mut cursor = 0;
    let degree = count(&args, &mut cursor);
    let point_count = count(&args, &mut cursor);
    let mut points = Vec::new();
    for _ in 0..point_count {
        points.push([next(&args, &mut cursor), next(&args, &mut cursor), next(&args, &mut cursor)]);
    }
    let knot_count = count(&args, &mut cursor);
    let knots = (0..knot_count).map(|_| next(&args, &mut cursor)).collect();
    let weight_count = count(&args, &mut cursor);
    let weights = (0..weight_count).map(|_| next(&args, &mut cursor)).collect();
    assert_eq!(cursor, args.len());
    let source = RationalCurve3 { degree, points, knots, weights };
    match rational_spans(&source) {
        Ok(spans) => {
            emit(1.0); emit(0.0); emit(spans.len() as f64);
            for span in spans {
                emit(span.start); emit(span.end); emit(span.curve.control.len() as f64);
                for h in span.curve.control { for value in h { emit(value); } }
            }
        }
        Err(failure) => {
            let code = match failure.0.as_str() {
                "A loft section contains an invalid spline." => 1.0,
                "Loft section weights must be finite and positive." => 2.0,
                "A loft section has an invalid knot domain." => 3.0,
                "Spline knot refinement failed." => 4.0,
                "Spline knot refinement encountered a zero span." => 5.0,
                "A loft section contains no nonzero curve spans." => 6.0,
                message => panic!("unmapped source refusal: {message}"),
            };
            emit(0.0); emit(code); emit(0.0);
        }
    }
}
