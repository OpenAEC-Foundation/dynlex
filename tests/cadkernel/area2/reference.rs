// SPDX-License-Identifier: MPL-2.0
// Decode inputs only; all geometric operations use unchanged pinned sources.
mod input {
    use crate::geom2d;
    include!("../cross2/reference.rs");
}
use input::{shape, number, integer, point};
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    let shape = shape(&args[1..]);
    let original = shape.clone();
    number(shape.enclosed_area());
    let chord = shape.chord_closed_area();
    integer(usize::from(chord.is_some()));
    if let Some(value) = chord { number(value); }
    let centre = shape.enclosed_centroid();
    integer(usize::from(centre.is_some()));
    if let Some(value) = centre { point(value); }
    let chord_centre = shape.chord_closed_centroid();
    integer(usize::from(chord_centre.is_some()));
    if let Some(value) = chord_centre { point(value); }
    integer(usize::from(shape == original));
}
