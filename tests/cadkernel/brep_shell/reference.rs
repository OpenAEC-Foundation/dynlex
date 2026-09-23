// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::brep::{
    make, shell, shell_face_at_point, transform, Body, Placement, ShellError, Snag,
};

fn picks(kind: usize, dimensions: [f64; 3]) -> Vec<[f64; 3]> {
    match kind {
        0 => vec![
            [0.0, dimensions[1] * 0.5, dimensions[2] * 0.5],
            [dimensions[0], dimensions[1] * 0.5, dimensions[2] * 0.5],
            [dimensions[0] * 0.5, 0.0, dimensions[2] * 0.5],
            [dimensions[0] * 0.5, dimensions[1], dimensions[2] * 0.5],
            [dimensions[0] * 0.5, dimensions[1] * 0.5, 0.0],
            [dimensions[0] * 0.5, dimensions[1] * 0.5, dimensions[2]],
        ],
        1 => vec![
            [dimensions[0], 0.0, dimensions[1] * 0.5],
            [0.0, 0.0, 0.0],
            [0.0, 0.0, dimensions[1]],
        ],
        2 => vec![[dimensions[0], 0.0, 0.0]],
        _ => Vec::new(),
    }
}

fn error_code(error: ShellError) -> (usize, usize) {
    match error {
        ShellError::InvalidDistance => (1, 0),
        ShellError::UnsupportedSolid => (2, 0),
        ShellError::UnknownFace => (3, 0),
        ShellError::NoMaterial => (4, 0),
        ShellError::Kernel(snag) => (
            5,
            match snag {
                Snag::NoClosedForm => 1,
                Snag::Coincident => 2,
                Snag::CutRefused => 3,
            },
        ),
    }
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let kind = values[0] as usize;
    let origin = [values[1], values[2], values[3]];
    let dimensions = [values[4], values[5], values[6]];
    let placement = Placement {
        x_axis: [values[7], values[8], values[9]],
        y_axis: [values[10], values[11], values[12]],
        z_axis: [values[13], values[14], values[15]],
        origin,
    };
    let distance = values[16];
    let mask = values[17] as u32;
    let local = match kind {
        0 => make::cuboid([0.0; 3], dimensions),
        1 => make::cylinder([0.0; 3], dimensions[0], dimensions[1]),
        2 => make::sphere([0.0; 3], dimensions[0]),
        3 => make::cone([0.0; 3], dimensions[0], dimensions[1]),
        _ => None,
    };
    let body = local
        .and_then(|body| transform(&body, &placement))
        .unwrap_or_default();
    let removed = picks(kind, dimensions)
        .into_iter()
        .enumerate()
        .filter(|(index, _)| mask & (1 << index) != 0)
        .filter_map(|(_, point)| shell_face_at_point(&body, placement.point(point)))
        .collect::<Vec<_>>();
    exact(removed.len());
    match shell(&body, &removed, distance) {
        Ok(result) => {
            flag(true);
            exact(0);
            exact(0);
            emit(&result);
        }
        Err(error) => {
            let (kind, snag) = error_code(error);
            flag(false);
            exact(kind);
            exact(snag);
            emit(&Body::new());
        }
    }
}
