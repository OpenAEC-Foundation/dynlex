// SPDX-License-Identifier: MPL-2.0
// Runtime driver for the unchanged pinned space/polygon.rs adapters.
fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let mode = values[0] as usize;
    let tolerance = geom2d::Tolerance::new(values[1]);
    let count = values[2] as usize;
    let mut offset = 3;
    let mut rings = Vec::new();
    for _ in 0..count {
        let size = values[offset] as usize;
        offset += 1;
        let mut ring = Vec::new();
        for _ in 0..size {
            ring.push([values[offset], values[offset + 1], values[offset + 2]]);
            offset += 3;
        }
        rings.push(ring);
    }
    let soup = if mode == 0 {
        space::polygon::triangulate(&rings[0], tolerance)
    } else {
        space::polygon::triangulate_rings(&rings, tolerance)
    };
    println!("{}", soup.len());
    for point in soup {
        for coordinate in point {
            println!("{coordinate:.17e}");
        }
    }
}
