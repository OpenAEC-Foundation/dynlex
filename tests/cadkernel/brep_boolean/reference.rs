// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::brep::make;
use cadkernel::brep::{combine, Operation, Snag};
use cadkernel::geom2d::{Curve, Line};

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let shape = values[14] as i32;
    let primitive_first = if shape == 1 {
        make::sphere([values[0], values[1], values[2]], values[3])
    } else {
        make::cuboid(
            [values[0], values[1], values[2]],
            [values[3], values[4], values[5]],
        )
    }
    .unwrap_or_default();
    let primitive_second = if shape == 1 {
        make::sphere([values[6], values[7], values[8]], values[9])
    } else if shape == 2 {
        make::cylinder(
            [values[6], values[7], values[8]],
            values[9],
            values[10],
        )
    } else {
        make::cuboid(
            [values[6], values[7], values[8]],
            [values[9], values[10], values[11]],
        )
    }
    .unwrap_or_default();
    let operation = match values[12] as i32 {
        0 => Operation::Union,
        1 => Operation::Intersection,
        2 => Operation::Difference,
        _ => unreachable!(),
    };
    let (first, second, tolerance) = if shape == 3 {
        let prism = |points: &[[f64; 2]]| {
            let profile = (0..points.len())
                .map(|index| Curve::Line(Line {
                    start: points[index],
                    end: points[(index + 1) % points.len()],
                }))
                .collect::<Vec<_>>();
            cadkernel::brep::extrude(
                cadkernel::space::Plane::XY,
                &profile,
                [0.0, 0.0, 3000.0],
            )
            .unwrap()
        };
        let outer = prism(&[
            [0.0, 2350.608306267448],
            [0.0, 6299.998941667038],
            [18119.99661333453, 6299.998941667038],
            [18119.99661333453, -299.99964722234654],
            [2650.609364600408, -299.99964722234654],
        ]);
        let inner = prism(&[
            [299.99964722234614, 2474.872823655858],
            [299.99964722234614, 5999.998941667038],
            [17819.995555001562, 5999.998941667038],
            [17819.995555001562, 0.0],
            [2774.8735292111646, 0.0],
        ]);
        let tolerance = cadkernel::brep::operation_tolerance(&[&outer, &inner]);
        let wall = combine(outer, inner, Operation::Difference, tolerance).unwrap();
        let cutter = make::cuboid(
            [6159.999129815122, -400.0, -100.0],
            [1200.0, 500.0, 2200.0],
        )
        .unwrap();
        (wall, cutter, tolerance)
    } else {
        (primitive_first, primitive_second, values[13])
    };
    match combine(first, second, operation, tolerance) {
        Ok(body) => {
            flag(true);
            exact(0);
            emit(&body);
        }
        Err(snag) => {
            flag(false);
            exact(match snag {
                Snag::NoClosedForm => 1,
                Snag::Coincident => 2,
                Snag::CutRefused => 3,
            });
            emit(&cadkernel::brep::Body::new());
        }
    }
}
