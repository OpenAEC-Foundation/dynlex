// SPDX-License-Identifier: MPL-2.0
// Pinned cadkernel src/brep/sweep.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::sweep_along_deformed;
use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line};
use cadkernel::space::Plane;

fn emit(value: f64) {
    println!("{value:.17e}");
}

fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|text| text.parse().expect("numeric argument")).collect();
    let mut cursor = 0;
    let mut next = || {
        let value = values[cursor];
        cursor += 1;
        value
    };
    let mut frame = || {
        Plane::from_axes(
            [next(), next(), next()],
            [next(), next(), next()],
            [next(), next(), next()],
        )
    };
    let profile_plane = frame();
    let path_plane = frame();
    let rotation = next();
    let twist = next();
    let scale = next();
    let mut read_curves = || {
        let count = next() as usize;
        (0..count).map(|_| match next() as u8 {
            0 => Curve2::Line(Line { start: [next(), next()], end: [next(), next()] }),
            1 => Curve2::Circle(Circle { centre: [next(), next()], radius: next() }),
            2 => Curve2::Arc(Arc {
                centre: [next(), next()], radius: next(),
                start_angle: next(), end_angle: next(),
            }),
            _ => panic!("unsupported curve kind"),
        }).collect::<Vec<_>>()
    };
    let profile = read_curves();
    let path = read_curves();
    assert_eq!(cursor, values.len(), "input consumed exactly");
    let result = sweep_along_deformed(profile_plane, &profile, path_plane, &path, rotation, twist, scale);
    emit(result.is_some() as u8 as f64);
    if let Some(body) = result {
        for count in [
            body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
            body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
            body.curves.len(), body.roots.len(), body.validate().len(),
            body.edges.iter().filter(|(_, edge)| edge.coedges.len() == 2).count(),
            body.coedges.iter().filter(|(_, coedge)| coedge.pcurve.is_some()).count(),
            body.faces.iter().filter(|(_, face)| face.forward).count(),
        ] {
            emit(count as f64);
        }
        for (_, vertex) in body.vertices.iter() {
            for coordinate in vertex.point {
                emit(coordinate);
            }
        }
    }
}
