// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{make, *};
use cadkernel::geom2d::{Arc, Curve, Line, NurbsCurve};
use cadkernel::space::{NurbsCurve3, Plane};

pub use cadkernel::brep::{
    extrude, operation_tolerance, planar_face_profile, Body, ChamferError, Curve3, EdgeKey,
    FaceKey, Surface,
};
mod geom2d {
    pub use cadkernel::geom2d::*;
}
mod space {
    pub use cadkernel::space::*;
}
#[path = "chamfer_profile_source.rs"]
mod chamfer_profile;
#[path = "chamfer_prismatic_source.rs"]
mod chamfer_prismatic;

fn number(value: f64) {
    println!("{value:.17e}");
}
fn exact(value: usize) {
    println!("{value}");
}
fn flag(value: bool) {
    exact(usize::from(value));
}
fn point<const N: usize>(value: [f64; N]) {
    for coordinate in value {
        number(coordinate);
    }
}
fn plane(value: &Plane) {
    point(value.origin);
    point(value.x_axis);
    point(value.y_axis);
}
fn provenance(value: Provenance) {
    exact(match value {
        Provenance::Synthesized => 0,
        Provenance::Clean(_) => 1,
        Provenance::Dirty(_) => 2,
    });
    exact(value.source().map(|source| source.index()).unwrap_or(0) as usize);
}
fn keys<T>(values: &[Key<T>]) {
    exact(values.len());
    for key in values {
        exact(key.slot() as usize);
    }
}
fn numbers(values: &[f64]) {
    exact(values.len());
    for &value in values {
        number(value);
    }
}
fn spline2(value: &NurbsCurve) {
    exact(value.degree());
    flag(value.is_closed());
    numbers(value.knots());
    numbers(value.weights());
    exact(value.control_points().len());
    for &control in value.control_points() {
        point(control);
    }
}
fn spline3(value: &NurbsCurve3) {
    exact(value.degree());
    flag(value.periodicity());
    numbers(value.knots());
    numbers(value.weights());
    exact(value.control_points().len());
    for &control in value.control_points() {
        point(control);
    }
}
fn pcurve(value: &Curve) {
    match value {
        Curve::Line(line) => {
            exact(0);
            point(line.start);
            point(line.end);
        }
        Curve::Circle(circle) => {
            exact(1);
            point(circle.centre);
            number(circle.radius);
        }
        Curve::Arc(arc) => {
            exact(2);
            point(arc.centre);
            number(arc.radius);
            number(arc.start_angle);
            number(arc.end_angle);
        }
        Curve::Ellipse(ellipse) => {
            exact(3);
            point(ellipse.ellipse.centre);
            point(ellipse.ellipse.major_axis);
            number(ellipse.ellipse.major_radius);
            number(ellipse.ellipse.minor_radius);
            number(ellipse.start_parameter);
            number(ellipse.end_parameter);
        }
        Curve::Polyline(polyline) => {
            exact(4);
            flag(polyline.closed);
            exact(polyline.vertices.len());
            for vertex in &polyline.vertices {
                point(vertex.position);
                number(vertex.bulge);
            }
        }
        Curve::Nurbs(curve) => {
            exact(5);
            spline2(curve);
        }
        Curve::Ray(ray) => {
            exact(6);
            point(ray.origin);
            point(ray.direction);
        }
        Curve::XLine(line) => {
            exact(7);
            point(line.base);
            point(line.direction);
        }
    }
}
fn emit(body: &Body) {
    provenance(body.provenance);
    keys(&body.roots);
    exact(body.vertices.len());
    for (key, node) in body.vertices.iter() {
        exact(key.slot() as usize);
        point(node.point);
        provenance(node.provenance);
    }
    exact(body.edges.len());
    for (key, node) in body.edges.iter() {
        exact(key.slot() as usize);
        exact(node.curve.slot() as usize);
        number(node.start_parameter);
        number(node.end_parameter);
        exact(node.start.slot() as usize);
        exact(node.end.slot() as usize);
        keys(&node.coedges);
        provenance(node.provenance);
    }
    exact(body.coedges.len());
    for (key, node) in body.coedges.iter() {
        exact(key.slot() as usize);
        exact(node.edge.slot() as usize);
        flag(node.forward);
        flag(node.pcurve.is_some());
        if let Some(curve) = &node.pcurve {
            pcurve(curve);
        }
        exact(node.owner.slot() as usize);
        provenance(node.provenance);
    }
    exact(body.loops.len());
    for (key, node) in body.loops.iter() {
        exact(key.slot() as usize);
        keys(&node.coedges);
        exact(node.owner.slot() as usize);
        provenance(node.provenance);
    }
    exact(body.faces.len());
    for (key, node) in body.faces.iter() {
        exact(key.slot() as usize);
        exact(node.surface.slot() as usize);
        flag(node.forward);
        keys(&node.loops);
        exact(node.owner.slot() as usize);
        provenance(node.provenance);
    }
    exact(body.shells.len());
    for (key, node) in body.shells.iter() {
        exact(key.slot() as usize);
        keys(&node.faces);
        exact(node.owner.slot() as usize);
        provenance(node.provenance);
    }
    exact(body.lumps.len());
    for (key, node) in body.lumps.iter() {
        exact(key.slot() as usize);
        keys(&node.shells);
        provenance(node.provenance);
    }
    exact(body.curves.len());
    for (key, node) in body.curves.iter() {
        exact(key.slot() as usize);
        match node {
            Curve3::Line(line) => {
                exact(0);
                point(line.origin);
                point(line.direction);
            }
            Curve3::Circle(circle) => {
                exact(1);
                plane(&circle.plane);
                number(circle.radius);
            }
            Curve3::Ellipse(ellipse) => {
                exact(2);
                plane(&ellipse.plane);
                number(ellipse.major_radius);
                number(ellipse.minor_radius);
            }
            Curve3::PlanarSpline { plane: frame, curve } => {
                exact(3);
                plane(frame);
                spline2(curve);
            }
            Curve3::Nurbs(curve) => {
                exact(4);
                spline3(curve);
            }
        }
    }
    exact(body.surfaces.len());
    for (key, node) in body.surfaces.iter() {
        exact(key.slot() as usize);
        match node {
            Surface::Plane(frame) => {
                exact(0);
                plane(frame);
            }
            Surface::Cylinder(cylinder) => {
                exact(1);
                plane(&cylinder.base);
                number(cylinder.radius);
            }
            Surface::Cone(cone) => {
                exact(2);
                plane(&cone.base);
                number(cone.radius);
                number(cone.half_angle);
            }
            Surface::Sphere(sphere) => {
                exact(3);
                plane(&sphere.frame);
                number(sphere.radius);
            }
            Surface::Torus(torus) => {
                exact(4);
                plane(&torus.frame);
                number(torus.major_radius);
                number(torus.minor_radius);
            }
            Surface::Nurbs(surface) => {
                exact(5);
                let (u, v) = surface.degrees();
                exact(u);
                exact(v);
                for periodic in surface.periodicity() {
                    flag(periodic);
                }
                flag(surface.v_reversed());
                let (u, v) = surface.knots();
                numbers(u);
                numbers(v);
                exact(surface.control_points().len());
                for (row, weights) in surface.control_points().iter().zip(surface.weights()) {
                    exact(row.len());
                    for &control in row {
                        point(control);
                    }
                    numbers(weights);
                }
            }
        }
    }
    exact(body.euler_characteristic() as usize);
    exact(body.validate().len());
    number(body.worst_vertex_gap());
}

fn rounded(origin: [f64; 3], width: f64, depth: f64, radius: f64, height: f64) -> Option<Body> {
    if !(width > radius * 2.0 && depth > radius * 2.0 && radius > 0.0) {
        return None;
    }
    let profile = vec![
        Curve::Line(Line { start: [radius, 0.0], end: [width - radius, 0.0] }),
        Curve::Arc(Arc { centre: [width - radius, radius], radius, start_angle: -std::f64::consts::FRAC_PI_2, end_angle: 0.0 }),
        Curve::Line(Line { start: [width, radius], end: [width, depth - radius] }),
        Curve::Arc(Arc { centre: [width - radius, depth - radius], radius, start_angle: 0.0, end_angle: std::f64::consts::FRAC_PI_2 }),
        Curve::Line(Line { start: [width - radius, depth], end: [radius, depth] }),
        Curve::Arc(Arc { centre: [radius, depth - radius], radius, start_angle: std::f64::consts::FRAC_PI_2, end_angle: std::f64::consts::PI }),
        Curve::Line(Line { start: [0.0, depth - radius], end: [0.0, radius] }),
        Curve::Arc(Arc { centre: [radius, radius], radius, start_angle: std::f64::consts::PI, end_angle: 3.0 * std::f64::consts::FRAC_PI_2 }),
    ];
    extrude(Plane::from_axes(origin, [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]), &profile, [0.0, 0.0, height])
}

fn damage(body: &mut Body, code: i32, value: f64) {
    let faces: Vec<_> = body.face_keys().collect();
    let edges: Vec<_> = body.edge_keys().collect();
    match code {
        1 => body.roots.clear(),
        2 => {
            if let Some(root) = body.roots.first().copied() {
                if let Some(lump) = body.lumps.get_mut(root) {
                    lump.shells.clear();
                }
            }
        }
        3 => {
            if let Some(root) = body.roots.first().copied() {
                let shell = body.lumps.get(root).and_then(|lump| lump.shells.first()).copied();
                if let Some(shell) = shell.and_then(|key| body.shells.get_mut(key)) {
                    shell.faces.clear();
                }
            }
        }
        4 => {
            if let Some(face) = faces.first().and_then(|key| body.faces.get_mut(*key)) {
                face.loops.clear();
            }
        }
        5 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get_mut(*key)) {
                edge.coedges.clear();
            }
        }
        6 => {
            if let Some(face) = faces.first().and_then(|key| body.faces.get(*key)) {
                let surface = face.surface;
                *body.surfaces.get_mut(surface).unwrap() = Surface::Torus(Torus { frame: Plane::XY, major_radius: 3.0, minor_radius: 1.0 });
            }
        }
        7 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get(*key)) {
                let curve = edge.curve;
                *body.curves.get_mut(curve).unwrap() = Curve3::Circle(Circle3 { plane: Plane::XY, radius: 2.0 });
            }
        }
        8 => {
            if let Some(face) = faces.first().and_then(|key| body.faces.get_mut(*key)) {
                if let Some(key) = face.loops.first().copied() {
                    face.loops.push(key);
                }
            }
        }
        9 => {
            if let Some(edge) = edges.first().and_then(|key| body.edges.get_mut(*key)) {
                if let Some(key) = edge.coedges.first().copied() {
                    edge.coedges.push(key);
                }
            }
        }
        10 => {
            let key = body.vertices.iter().next().map(|(key, _)| key);
            if let Some(vertex) = key.and_then(|key| body.vertices.get_mut(key)) {
                vertex.point[0] += value;
            }
        }
        _ => {}
    }
}

fn error(error: ChamferError, selected: &[EdgeKey]) -> (usize, usize) {
    match error {
        ChamferError::EdgeOutsideBaseFace(edge) => (1, usize::from(selected.contains(&edge))),
        ChamferError::DistanceTooLargeOrInteracting => (2, 0),
        ChamferError::UnsupportedBodySurface => (3, 0),
        ChamferError::InvalidResult => (4, 0),
        other => panic!("unexpected prismatic error: {other:?}"),
    }
}

fn main() {
    let values: Vec<f64> = std::env::args().skip(1).map(|value| value.parse().unwrap()).collect();
    let origin = [values[1], values[2], values[3]];
    let mut body = match values[0] as i32 {
        0 => make::cuboid(origin, [values[4], values[5], values[6]]),
        1 => make::cylinder(origin, values[4], values[5]),
        2 => make::sphere(origin, values[4]),
        3 => make::cone(origin, values[4], values[5]),
        4 => make::wedge(origin, values[4], values[5], values[6]),
        5 => make::pyramid_frustum(origin, values[4], values[4], values[5], values[6] as usize),
        6 => rounded(origin, values[4], values[5], values[6], values[7]),
        _ => None,
    }
    .unwrap_or_default();
    let edges: Vec<_> = body.edge_keys().collect();
    let faces: Vec<_> = body.face_keys().collect();
    let count = values[8] as usize;
    let selected: Vec<_> = (0..count)
        .filter_map(|index| {
            (!edges.is_empty()).then(|| edges[(values[9 + index].abs() as usize) % edges.len()])
        })
        .collect();
    let base = if faces.is_empty() {
        None
    } else {
        Some(faces[(values[12].abs() as usize) % faces.len()])
    };
    damage(&mut body, values[15] as i32, values[16]);
    let result = base.and_then(|base| {
        chamfer_prismatic::chamfer_prismatic(&body, &selected, base, values[13], values[14])
    });
    match result {
        None => {
            flag(false);
            flag(false);
            exact(0);
            exact(0);
        }
        Some(Err(problem)) => {
            let (kind, edge) = error(problem, &selected);
            flag(true);
            flag(false);
            exact(kind);
            exact(edge);
        }
        Some(Ok(result)) => {
            flag(true);
            flag(true);
            exact(0);
            exact(0);
            emit(&result);
        }
    }
}
