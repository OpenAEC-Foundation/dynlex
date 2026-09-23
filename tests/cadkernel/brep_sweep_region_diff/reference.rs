// SPDX-License-Identifier: MPL-2.0
// Pinned source: cadkernel src/brep/{sweep,presspull}.rs at
// 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{extrude_region, extrude_surface_region, extrude_surface_region_tapered, Body, Surface};
use cadkernel::geom2d::{Arc, Circle, Curve as Curve2, Line, Polyline, PolylineVertex as Vertex2};
use cadkernel::space::Plane;

fn number(value: f64) {
    println!("{value:.17e}");
}

fn emit_body(body: &Body) {
    for count in [
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(),
        body.curves.len(), body.roots.len(), body.validate().len(),
    ] {
        number(count as f64);
    }
    for root in &body.roots {
        let lump = body.lumps.get(*root).expect("live root");
        number(lump.shells.len() as f64);
        for shell_key in &lump.shells {
            let shell = body.shells.get(*shell_key).expect("live shell");
            number(shell.faces.len() as f64);
        }
    }
    for (_, vertex) in body.vertices.iter() {
        for coordinate in vertex.point {
            number(coordinate);
        }
    }
    for (_, surface) in body.surfaces.iter() {
        let kind = match surface {
            Surface::Plane(_) => 0,
            Surface::Cylinder(_) => 1,
            Surface::Cone(_) => 2,
            Surface::Sphere(_) => 3,
            Surface::Torus(_) => 4,
            Surface::Nurbs(_) => 5,
        };
        number(kind as f64);
        if let Surface::Nurbs(shape) = surface {
            let (u, v) = shape.degrees();
            number(u as f64);
            number(v as f64);
            let (u_knots, v_knots) = shape.knots();
            for knots in [u_knots, v_knots] {
                number(knots.len() as f64);
                for knot in knots {
                    number(*knot);
                }
            }
            number(shape.control_points().len() as f64);
            for row in shape.control_points() {
                number(row.len() as f64);
                for point in row {
                    for coordinate in point {
                        number(*coordinate);
                    }
                }
            }
            number(shape.weights().len() as f64);
            for row in shape.weights() {
                number(row.len() as f64);
                for weight in row {
                    number(*weight);
                }
            }
        }
    }
    for (_, face) in body.faces.iter() {
        number(face.forward as u8 as f64);
        number(face.loops.len() as f64);
    }
    for (_, coedge) in body.coedges.iter() {
        number(coedge.forward as u8 as f64);
        match &coedge.pcurve {
            None => number(0.0),
            Some(Curve2::Line(line)) => {
                number(1.0);
                for coordinate in [line.start[0], line.start[1], line.end[0], line.end[1]] {
                    number(coordinate);
                }
            }
            Some(Curve2::Arc(arc)) => {
                number(2.0);
                for coordinate in [arc.centre[0], arc.centre[1], arc.radius, arc.start_angle, arc.end_angle] {
                    number(coordinate);
                }
            }
            Some(Curve2::Circle(circle)) => {
                number(3.0);
                for coordinate in [circle.centre[0], circle.centre[1], circle.radius] {
                    number(coordinate);
                }
            }
            Some(_) => number(4.0),
        }
    }
}

fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|text| text.parse().expect("numeric argument")).collect();
    let mut cursor = 0;
    let mut next = || {
        let value = values[cursor];
        cursor += 1;
        value
    };
    let mode = next() as u8;
    let direction = [next(), next(), next()];
    let angle = next();
    let loop_count = next() as usize;
    let mut profiles = Vec::with_capacity(loop_count);
    for _ in 0..loop_count {
        let piece_count = next() as usize;
        let mut profile = Vec::with_capacity(piece_count);
        for _ in 0..piece_count {
            let kind = next() as u8;
            let piece = match kind {
                0 => Curve2::Line(Line { start: [next(), next()], end: [next(), next()] }),
                1 => Curve2::Circle(Circle { centre: [next(), next()], radius: next() }),
                2 => Curve2::Arc(Arc {
                    centre: [next(), next()], radius: next(), start_angle: next(), end_angle: next(),
                }),
                4 => {
                    let closed = next() != 0.0;
                    let count = next() as usize;
                    let vertices = (0..count)
                        .map(|_| Vertex2 { position: [next(), next()], bulge: next() })
                        .collect();
                    Curve2::Polyline(Polyline { vertices, closed })
                }
                _ => panic!("unsupported curve kind"),
            };
            profile.push(piece);
        }
        profiles.push(profile);
    }
    assert_eq!(cursor, values.len(), "input consumed exactly");
    let plane = Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let result = match mode {
        0 => extrude_surface_region(plane, &profiles, direction),
        1 => extrude_surface_region_tapered(plane, &profiles, direction, angle),
        2 => extrude_region(plane, &profiles, direction),
        _ => panic!("unsupported sweep mode"),
    };
    number(result.is_some() as u8 as f64);
    if let Some(body) = result {
        emit_body(&body);
    }
}
