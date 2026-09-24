// SPDX-License-Identifier: MPL-2.0
// Calls the public history entry points from the pinned, unmodified crate.
use acadrust::objects::{SolidHistoryBox, SolidHistoryNodeBase, SolidHistoryOperation};
use cadkernel::acis::{rebuild_body, rebuild_history, HistoryRebuildError};
include!("emit.rs");

fn argument_number(args: &[String], index: usize) -> f64 {
    args[index].parse().expect("numeric argument")
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    assert_eq!(args.len(), 21);
    let size = [argument_number(&args, 2), argument_number(&args, 3), argument_number(&args, 4)];
    let matrix = std::array::from_fn(|index| argument_number(&args, index + 5));
    let operation = match args[1].as_str() {
        "0" => SolidHistoryOperation::Unknown,
        "1" => SolidHistoryOperation::Box(SolidHistoryBox {
            base: SolidHistoryNodeBase {
                transform: matrix,
                ..SolidHistoryNodeBase::new(42)
            },
            operation_major: 7,
            operation_minor: 3,
            length: size[0],
            width: size[1],
            height: size[2],
        }),
        "2" => SolidHistoryOperation::Wedge(SolidHistoryBox {
            base: SolidHistoryNodeBase {
                transform: matrix,
                ..SolidHistoryNodeBase::new(42)
            },
            operation_major: 7,
            operation_minor: 3,
            length: size[0],
            width: size[1],
            height: size[2],
        }),
        _ => panic!("reference only accepts Unknown, Box and Wedge"),
    };
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
        Err(HistoryRebuildError::Unsupported) => exact(3),
        Err(other) => panic!("unexpected error: {other:?}"),
    }
}
