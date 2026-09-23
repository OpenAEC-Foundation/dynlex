// SPDX-License-Identifier: MPL-2.0
use cadkernel::geom2d::{offset_polyline, Polyline, PolylineVertex};

fn number(value: f64) {
    println!("{value:.17}");
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let closed = values[0] != 0.0;
    let distance = values[1];
    let side = [values[2], values[3]];
    let count = values[4] as usize;
    let vertices = (0..count)
        .map(|index| {
            let at = 5 + index * 3;
            PolylineVertex {
                position: [values[at], values[at + 1]],
                bulge: values[at + 2],
            }
        })
        .collect();
    let result = offset_polyline(&Polyline { vertices, closed }, distance, side);
    println!("{}", result.len());
    for polyline in result {
        println!("{}", i32::from(polyline.closed));
        println!("{}", polyline.vertices.len());
        for vertex in polyline.vertices {
            number(vertex.position[0]);
            number(vertex.position[1]);
            number(vertex.bulge);
        }
    }
}
