// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::brep::make;
use cadkernel::brep::{imprint, Snag, Surface, Torus};

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let mut first = make::cuboid(
        [values[0], values[1], values[2]],
        [values[3], values[4], values[5]],
    )
    .unwrap_or_default();
    let mut second = make::cuboid(
        [values[6], values[7], values[8]],
        [values[9], values[10], values[11]],
    )
    .unwrap_or_default();
    if values[13] == 1.0 {
        let selected = second.face_keys().next();
        if let Some(face) = selected {
            let surface = second.faces.get(face).unwrap().surface;
            *second.surfaces.get_mut(surface).unwrap() = Surface::Torus(Torus {
                frame: Plane::XY,
                major_radius: 4.0,
                minor_radius: 1.0,
            });
        }
    }
    match imprint(&mut first, &mut second, values[12]) {
        Ok(result) => {
            flag(true);
            exact(0);
            exact(result.cuts);
            exact(result.meetings);
        }
        Err(snag) => {
            flag(false);
            exact(match snag {
                Snag::NoClosedForm => 1,
                Snag::Coincident => 2,
                Snag::CutRefused => 3,
            });
            exact(0);
            exact(0);
        }
    }
    emit(&first);
    emit(&second);
}
