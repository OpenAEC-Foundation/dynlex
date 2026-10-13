// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{extrude_surface_tapered, Body, Surface};
use cadkernel::geom2d::{Arc, Curve as Curve2, Line};
use cadkernel::space::Plane;

fn number(value: f64) {
    println!("{value:.17e}");
}

fn emit(body: &Body) {
    for count in [
        body.vertices.len(),
        body.edges.len(),
        body.coedges.len(),
        body.loops.len(),
        body.faces.len(),
        body.shells.len(),
        body.lumps.len(),
        body.surfaces.len(),
        body.curves.len(),
        body.roots.len(),
        body.validate().len(),
    ] {
        number(count as f64);
    }
    for (_, vertex) in body.vertices.iter() {
        for value in vertex.point {
            number(value);
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
                    for value in point {
                        number(*value);
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
        if let Some(Curve2::Line(line)) = &coedge.pcurve {
            number(1.0);
            for value in [line.start[0], line.start[1], line.end[0], line.end[1]] {
                number(value);
            }
        } else {
            number(0.0);
        }
    }
}

fn main() {
    let frame = Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let cases = [
        (
            vec![Curve2::Line(Line {
                start: [0.0, 0.0],
                end: [4.0, 0.0],
            })],
            [0.0, 0.0, 3.0],
            0.2,
        ),
        (
            vec![Curve2::Arc(Arc {
                centre: [0.0, 0.0],
                radius: 2.0,
                start_angle: 0.0,
                end_angle: std::f64::consts::FRAC_PI_2,
            })],
            [0.0, 0.0, 1.0],
            0.1,
        ),
        (
            vec![
                Curve2::Line(Line {
                    start: [0.0, 0.0],
                    end: [4.0, 0.0],
                }),
                Curve2::Line(Line {
                    start: [4.0, 4.0],
                    end: [4.0, 0.0],
                }),
            ],
            [0.0, 0.0, 1.0],
            0.1,
        ),
    ];
    for (index, (profile, direction, angle)) in cases.into_iter().enumerate() {
        number(index as f64);
        let result = extrude_surface_tapered(frame, &profile, direction, angle);
        number(result.is_some() as u8 as f64);
        if let Some(body) = result {
            emit(&body);
        }
    }
}
