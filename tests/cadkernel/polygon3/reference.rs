// SPDX-License-Identifier: MPL-2.0
// The generated driver imports the pinned, unchanged source modules.
fn emit(value: f64) { println!("{value:.17e}"); }
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let count = args[0] as usize;
    assert_eq!(args.len(), 1 + 3 * count);
    let ring: Vec<[f64; 3]> = args[1..].chunks_exact(3).map(|v| [v[0], v[1], v[2]]).collect();
    for value in space::polygon::area_vector(&ring) { emit(value); }
    emit(space::polygon::area(&ring));
    let normal = space::polygon::normal(&ring);
    emit(if normal.is_some() { 1.0 } else { 0.0 });
    if let Some(normal) = normal { for value in normal { emit(value); } }
    emit(space::polygon::perimeter(&ring));
    emit(space::polygon::chain_length(&ring));
}
