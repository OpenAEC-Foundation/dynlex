// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::brep::make;
use cadkernel::brep::split::{split_edge, split_edge_at, split_face};
use cadkernel::brep::{
    Body, Circle3, Coedge, Curve3, Cylinder, Edge, Ellipse3, Face, Line3, Loop, Lump,
    Provenance, Shell, Surface, Vertex,
};

fn periodic_band(origin: [f64; 3], radius: f64, height: f64) -> (Body, cadkernel::brep::FaceKey) {
    let mut body = Body::new();
    let provenance = Provenance::Synthesized;
    let lump = body.lumps.insert(Lump { shells: Vec::new(), provenance });
    body.roots.push(lump);
    let shell = body.shells.insert(Shell { faces: Vec::new(), owner: lump, provenance });
    body.lumps.get_mut(lump).unwrap().shells.push(shell);
    let base = Plane::orthonormal(origin, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]).unwrap();
    let surface = body.surfaces.insert(Surface::Cylinder(Cylinder { base, radius }));
    let face = body.faces.insert(Face {
        surface, forward: true, loops: Vec::new(), owner: shell, provenance,
    });
    body.shells.get_mut(shell).unwrap().faces.push(face);
    for (z, forward) in [(origin[2], false), (origin[2] + height, true)] {
        let plane = Plane::orthonormal(
            [origin[0], origin[1], z], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0],
        ).unwrap();
        let vertex = body.vertices.insert(Vertex {
            point: [origin[0] + radius, origin[1], z], provenance,
        });
        let curve = body.curves.insert(Curve3::Circle(Circle3 { plane, radius }));
        let edge = body.edges.insert(Edge {
            curve, start_parameter: 0.0, end_parameter: std::f64::consts::TAU,
            start: vertex, end: vertex, coedges: Vec::new(), provenance,
        });
        let ring = body.loops.insert(Loop { coedges: Vec::new(), owner: face, provenance });
        let coedge = body.coedges.insert(Coedge {
            edge, forward, pcurve: None, owner: ring, provenance,
        });
        body.edges.get_mut(edge).unwrap().coedges.push(coedge);
        body.loops.get_mut(ring).unwrap().coedges.push(coedge);
        body.faces.get_mut(face).unwrap().loops.push(ring);
    }
    (body, face)
}

fn bottom_face(body: &cadkernel::brep::Body, z: f64, tolerance: f64) -> Option<cadkernel::brep::FaceKey> {
    body.face_keys().find(|key| {
        let Some(face) = body.faces.get(*key) else { return false };
        let Some(Surface::Plane(plane)) = body.surfaces.get(face.surface) else { return false };
        (plane.origin[2] - z).abs() <= tolerance
            && plane.normal().is_some_and(|normal| normal[2].abs() > 0.9)
    })
}

fn face_cutter(values: &[f64], fraction: f64, style: i32) -> Curve3 {
    let origin = [values[4], values[5], values[6]];
    let size = [values[1], values[2], values[3]];
    let (line_origin, direction) = match style {
        0 => ([origin[0] - size[0], origin[1] + fraction * size[1], origin[2]], [1.0, 0.0, 0.0]),
        1 => ([origin[0] + fraction * size[0], origin[1] - size[1], origin[2]], [0.0, 1.0, 0.0]),
        2 => (origin, [size[0], size[1], 0.0]),
        3 => ([origin[0] - size[0], origin[1] + (2.0 + fraction.abs()) * size[1], origin[2]], [1.0, 0.0, 0.0]),
        4 => ([origin[0] - size[0], origin[1] + fraction * size[1], origin[2] + 0.1 * size[2]], [1.0, 0.0, 0.0]),
        _ => unreachable!(),
    };
    Curve3::Line(Line3 { origin: line_origin, direction })
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let origin = [values[4], values[5], values[6]];
    if values[0] as i32 == 6 {
        let size = [values[1], values[2], values[3]];
        let mut body = make::cuboid(origin, size).unwrap_or_default();
        let tolerance = values[9];
        let centre = [origin[0] + 0.5 * size[0], origin[1] + 0.5 * size[1], origin[2]];
        let cutter = Plane::orthonormal(centre, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0])
            .map(|plane| Curve3::Ellipse(Ellipse3 {
                plane, major_radius: values[8], minor_radius: values[11],
            }));
        let face = bottom_face(&body, origin[2], tolerance);
        let first = face.zip(cutter.as_ref()).and_then(|(face, cutter)| {
            split_face(&mut body, face, cutter, tolerance)
        });
        flag(first.is_some());
        let second = if values[10] == 1.0 {
            first.and_then(|[kept, made]| {
                let cutter = cutter.as_ref()?;
                split_face(&mut body, kept, cutter, tolerance)
                    .or_else(|| split_face(&mut body, made, cutter, tolerance))
            })
        } else {
            None
        };
        flag(second.is_some());
        emit(&body);
        return;
    }
    if values[0] as i32 == 5 {
        let radius = values[1];
        let height = values[2];
        let (mut body, face) = periodic_band(origin, radius, height);
        let section = |fraction: f64| {
            let z = origin[2] + fraction * height;
            Plane::orthonormal(
                [origin[0], origin[1], z], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0],
            ).map(|plane| Curve3::Circle(Circle3 { plane, radius }))
        };
        let first_cutter = section(values[8]);
        let first = first_cutter.as_ref().and_then(|cutter| {
            split_face(&mut body, face, cutter, values[9])
        });
        flag(first.is_some());
        let second = if values[10] == 1.0 {
            first.and_then(|[kept, made]| {
                let cutter = section(values[11])?;
                split_face(&mut body, kept, &cutter, values[9])
                    .or_else(|| split_face(&mut body, made, &cutter, values[9]))
            })
        } else {
            None
        };
        flag(second.is_some());
        emit(&body);
        return;
    }
    if values[0] as i32 == 4 {
        let size = [values[1], values[2], values[3]];
        let mut body = make::cuboid(origin, size).unwrap_or_default();
        let tolerance = values[9];
        let centre = [origin[0] + 0.5 * size[0], origin[1] + 0.5 * size[1], origin[2]];
        let cutter = Plane::orthonormal(centre, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0])
            .map(|plane| Curve3::Circle(Circle3 { plane, radius: values[8] }));
        let face = bottom_face(&body, origin[2], tolerance);
        let first = face.zip(cutter.as_ref()).and_then(|(face, cutter)| {
            split_face(&mut body, face, cutter, tolerance)
        });
        flag(first.is_some());
        let second = if values[10] == 1.0 {
            first.and_then(|[kept, made]| {
                let cutter = cutter.as_ref()?;
                split_face(&mut body, kept, cutter, tolerance)
                    .or_else(|| split_face(&mut body, made, cutter, tolerance))
            })
        } else {
            None
        };
        flag(second.is_some());
        emit(&body);
        return;
    }
    if values[0] as i32 == 3 {
        let radius = values[1];
        let mut body = make::sphere(origin, radius).unwrap_or_default();
        let offset = values[8] * radius;
        let cutter = Plane::orthonormal(
            [origin[0], origin[1], origin[2] + offset],
            [1.0, 0.0, 0.0],
            [0.0, 0.0, 1.0],
        )
        .map(|plane| Curve3::Circle(Circle3 {
            plane,
            radius: (radius * radius - offset * offset).max(0.0).sqrt(),
        }));
        let face = body.face_keys().next();
        let first = face.zip(cutter.as_ref()).and_then(|(face, cutter)| {
            split_face(&mut body, face, cutter, values[9])
        });
        flag(first.is_some());
        let second = if values[10] == 1.0 {
            first.and_then(|[kept, made]| {
                let cutter = cutter.as_ref()?;
                split_face(&mut body, kept, cutter, values[9])
                    .or_else(|| split_face(&mut body, made, cutter, values[9]))
            })
        } else {
            None
        };
        flag(second.is_some());
        emit(&body);
        return;
    }
    if values[0] as i32 == 2 {
        let mut body = make::cuboid(origin, [values[1], values[2], values[3]]).unwrap_or_default();
        let tolerance = values[9];
        let style = values[7] as i32;
        let first = bottom_face(&body, origin[2], tolerance).and_then(|face| {
            split_face(&mut body, face, &face_cutter(&values, values[8], style), tolerance)
        });
        flag(first.is_some());
        let second = if values[10] == 1.0 {
            first.and_then(|[kept, made]| {
                let cutter = face_cutter(&values, values[11], 0);
                split_face(&mut body, kept, &cutter, tolerance)
                    .or_else(|| split_face(&mut body, made, &cutter, tolerance))
            })
        } else {
            None
        };
        flag(second.is_some());
        emit(&body);
        return;
    }
    let mut body = match values[0] as i32 {
        0 => make::cuboid(origin, [values[1], values[2], values[3]]),
        1 => make::cylinder(origin, values[1], values[2]),
        _ => None,
    }
    .unwrap_or_default();
    let edges = body.edge_keys().collect::<Vec<_>>();
    let first = edges.get((values[7].abs() as usize) % edges.len()).and_then(|edge| {
        let node = body.edges.get(*edge)?.clone();
        let parameter = node.start_parameter + values[8] * (node.end_parameter - node.start_parameter);
        if values[9] == 0.0 {
            split_edge(&mut body, *edge, parameter)
        } else {
            let point = body.curves.get(node.curve)?.point_at(parameter);
            split_edge_at(&mut body, *edge, point)
        }
    });
    flag(first.is_some());
    let second = first.and_then(|(near, far)| {
        let target = if values[10] == 1.0 {
            near
        } else if values[10] == 2.0 {
            far
        } else {
            return None;
        };
        let node = body.edges.get(target)?.clone();
        let parameter = node.start_parameter + values[11] * (node.end_parameter - node.start_parameter);
        split_edge(&mut body, target, parameter)
    });
    flag(second.is_some());
    emit(&body);
}
