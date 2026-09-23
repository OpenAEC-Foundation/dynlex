// SPDX-License-Identifier: MPL-2.0
// Geometry comes exclusively from the complete pinned source modules.
fn emit(label: &str, index: usize, value: f64) {
    println!("{label}.{index} {value:.17e}");
}
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let (ns, nt) = (args[0] as usize, args[1] as usize);
    assert_eq!(args.len(), 6 + 3 * (ns + nt));
    let triple = |i| [args[i], args[i + 1], args[i + 2]];
    let source: Vec<_> = (0..ns).map(|i| triple(3 + i * 3)).collect();
    let target: Vec<_> = (0..nt).map(|i| triple(3 + (ns + i) * 3)).collect();
    let query = triple(3 + (ns + nt) * 3);
    let result = space::alignment::align_point_pairs(&source, &target, args[2] != 0.0);
    emit("valid", 0, if result.is_some() { 1.0 } else { 0.0 });
    if let Some(matrix) = result {
        for (i, value) in matrix.iter().flatten().enumerate() { emit("matrix", i, *value); }
        // Same row-major multiplication specified by the original test.
        for (i, point) in source.iter().chain(std::iter::once(&query)).enumerate() {
            for row in 0..3 {
                emit("mapped", 3 * i + row, matrix[row][0] * point[0]
                    + matrix[row][1] * point[1] + matrix[row][2] * point[2] + matrix[row][3]);
            }
        }
    }
    for (i, point) in source.iter().chain(&target).enumerate() {
        for (axis, value) in point.iter().enumerate() { emit("input", i * 3 + axis, *value); }
    }
}
