// SPDX-License-Identifier: MPL-2.0
// Included by a generated driver that imports the unchanged upstream module.
use std::cell::Cell;
use std::f64::consts::TAU;

fn emit(value: f64) { println!("{value:.17e}"); }

fn directions<const N: usize>(input: &[f64]) {
    let left = std::array::from_fn(|axis| input[3 + axis]);
    let right = std::array::from_fn(|axis| input[3 + N + axis]);
    emit(tessellation::direction_angle::<N>(left, right));
    emit(tessellation::max_direction_angle::<N>(&[]));
    emit(tessellation::max_direction_angle(&[left]));
    emit(tessellation::max_direction_angle(&[left, right, left]));
}

fn main() {
    let input: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    if input[0] == 0.0 {
        emit(tessellation::DEFAULT_ANGLE);
        emit(tessellation::DISPLAY_ANGLE);
        emit(tessellation::FINE_ANGLE);
        emit(tessellation::angle(input[2]));
        emit(tessellation::angle_for_resolution(input[2]));
        emit(tessellation::display_angle_for_resolution(input[2]));
        match input[1] as usize {
            0 => directions::<0>(&input),
            1 => directions::<1>(&input),
            2 => directions::<2>(&input),
            3 => directions::<3>(&input),
            5 => directions::<5>(&input),
            17 => directions::<17>(&input),
            _ => panic!("unsupported probe dimension"),
        }
    } else {
        let kind = input[1] as i32;
        let turns = input[2];
        let point_calls = Cell::new(0usize);
        let tangent_calls = Cell::new(0usize);
        let first_tangent = Cell::new(0.0);
        let last_tangent = Cell::new(0.0);
        let points = tessellation::sample_curve3_angle(
            |parameter| {
                point_calls.set(point_calls.get() + 1);
                if kind == 0 { return [parameter, 0.0, 0.0]; }
                let angle = (parameter * turns) * TAU;
                [angle.cos(), angle.sin(), parameter]
            },
            |parameter| {
                if tangent_calls.get() == 0 { first_tangent.set(parameter); }
                last_tangent.set(parameter);
                tangent_calls.set(tangent_calls.get() + 1);
                match kind {
                    0 => [1.0, 0.0, 0.0],
                    2 => [0.0, 0.0, 0.0],
                    3 => [f64::NAN, 1.0, 0.0],
                    _ => {
                        let rate = turns * TAU;
                        let angle = parameter * rate;
                        [-angle.sin() * rate, angle.cos() * rate, 1.0]
                    }
                }
            },
            input[3],
        );
        emit(points.len() as f64);
        emit(point_calls.get() as f64);
        emit(tangent_calls.get() as f64);
        emit(first_tangent.get());
        emit(last_tangent.get());
        for point in points { for value in point { emit(value); } }
    }
}
