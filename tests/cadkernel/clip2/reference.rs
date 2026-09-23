// SPDX-License-Identifier: MPL-2.0
// Share only input decoding and output formatting with the crossing driver.
mod input {
    use crate::geom2d;
    include!("../cross2/reference.rs");
}
use input::{shape, number, integer, point};
fn spans(values: &[[f64; 2]]) {
    integer(values.len());
    for span in values { number(span[0]); number(span[1]); }
}
fn main() {
    let a: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    let tolerance = geom2d::Tolerance::new(a[0]);
    let count = a[4] as usize;
    let cuts = &a[5..5+count];
    let mut base = 5 + count;
    let boundary_count = a[base] as usize;
    base += 1;
    let size = a[base] as usize;
    let curve = shape(&a[base+1..base+1+size]);
    base += size + 1;
    let mut boundary = Vec::new();
    for _ in 0..boundary_count {
        let size = a[base] as usize;
        boundary.push(shape(&a[base+1..base+1+size]));
        base += size + 1;
    }
    let broken = geom2d::clip::break_spans(&curve, a[1], a[2], tolerance);
    integer(usize::from(broken.is_some()));
    if let Some(values) = broken { spans(&values); }
    spans(&geom2d::clip::trim_spans(&curve, cuts, a[3], tolerance));
    spans(&geom2d::clip::inside_spans(&boundary, &curve, tolerance));
    let pieces = geom2d::clip::inside_pieces(&boundary, &curve, tolerance);
    integer(pieces.len());
    for piece in pieces { point(piece[0]); point(piece[1]); }
}
