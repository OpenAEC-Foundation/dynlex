// SPDX-License-Identifier: MPL-2.0
// Calls the public pinned Rust history entry points without changing them.
use acadrust::objects::{SolidHistoryCylinder, SolidHistoryNodeBase, SolidHistoryOperation};
use cadkernel::acis::{rebuild_body, rebuild_history, HistoryRebuildError};
include!("emit.rs");

fn argument_number(args: &[String], index: usize) -> f64 {
    args[index].parse().expect("numeric argument")
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    assert_eq!(args.len(), 21);
    let matrix = std::array::from_fn(|index| argument_number(&args, index + 5));
    let operation = SolidHistoryOperation::Cylinder(SolidHistoryCylinder {
        base: SolidHistoryNodeBase {
            transform: matrix,
            ..SolidHistoryNodeBase::new(42)
        },
        operation_major: 7,
        operation_minor: 3,
        major_radius: argument_number(&args, 1),
        minor_radius: argument_number(&args, 2),
        x_radius: argument_number(&args, 3),
        height: argument_number(&args, 4),
    });
    let rebuilt = match args[0].as_str() {
        "body" => rebuild_body(&operation),
        "history" => rebuild_history(&[operation]),
        _ => panic!("unknown reference route"),
    };
    match rebuilt {
        Ok(body) => {
            exact(0);
            emit(&body);
        }
        Err(HistoryRebuildError::InvalidTransform) => exact(1),
        Err(HistoryRebuildError::InvalidParameters) => exact(2),
        Err(other) => panic!("unexpected error: {other:?}"),
    }
}
