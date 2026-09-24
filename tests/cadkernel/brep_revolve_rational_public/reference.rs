// SPDX-License-Identifier: MPL-2.0
// Public topology from pinned src/brep/sweep.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{revolve, revolve_surface, Body, Surface};
use cadkernel::geom2d::{Curve as Curve2, Ellipse, EllipseArc, Line, NurbsCurve};
use cadkernel::space::Plane;

fn emit(value: f64) {
    println!("{value:.17e}");
}

fn emit_body(body: &Body) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
    ] {
        emit(count as f64);
    }
    emit(body.surfaces.iter().filter(|(_, s)| matches!(s, Surface::Nurbs(_))).count() as f64);
    emit(body.coedges.iter().filter(|(_, c)| c.pcurve.is_some()).count() as f64);
    for (_, use_node) in body.coedges.iter() {
        emit(use_node.forward as u8 as f64);
        if let Some(Curve2::Line(line)) = &use_node.pcurve {
            emit(1.0);
            for coordinate in line.start.into_iter().chain(line.end) {
                emit(coordinate);
            }
        } else {
            emit(0.0);
        }
    }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point { emit(coordinate); }
    }
    for (_, face) in body.faces.iter() {
        emit(face.forward as u8 as f64);
        emit(face.loops.len() as f64);
    }
}

fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    let mode = args[0] as i32;
    let kind = args[1] as i32;
    let angle = args[2];
    let side = args[3];
    let axis_end = args[4] != 0.0;
    let curve = match kind {
        3 => Curve2::Ellipse(EllipseArc {
            ellipse: Ellipse { centre: [3.0 * side, 1.0], major_radius: 1.0,
                minor_radius: 0.5, major_axis: [1.0, 0.0] },
            start_parameter: 0.0, end_parameter: std::f64::consts::PI,
        }),
        5 => Curve2::Nurbs(NurbsCurve::new_strict(
            2, vec![[if axis_end { 0.0 } else { side }, 0.0], [4.0 * side, 2.0], [side, 4.0]],
            vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], vec![1.0, 1.0, 1.0],
        ).unwrap()),
        _ => panic!("unsupported curve kind"),
    };
    let closing = if kind == 3 {
        Line { start: [3.0 * side - 1.0, 1.0], end: [3.0 * side + 1.0, 1.0] }
    } else {
        Line { start: [side, 4.0], end: [if axis_end { 0.0 } else { side }, 0.0] }
    };
    let mut profile = vec![curve];
    if mode == 1 { profile.push(Curve2::Line(closing)); }
    let plane = Plane::orthonormal([0.0; 3], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0]).unwrap();
    let result = match mode {
        0 => revolve_surface(plane, &profile, [0.0; 3], [0.0, 0.0, 1.0], angle),
        1 => revolve(plane, &profile, [0.0; 3], [0.0, 0.0, 1.0], angle),
        _ => panic!("unsupported operation"),
    };
    emit(result.is_some() as u8 as f64);
    if let Some(body) = result { emit_body(&body); }
}
