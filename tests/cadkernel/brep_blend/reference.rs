// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{
    chamfer_edges, extrude_surface, fillet_edges, make, Body, ChamferError, Curve3, EdgeKey,
    FilletError, Surface,
};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

fn number(value: f64) {
    println!("{value:.17}");
}

fn exact(value: usize) {
    println!("{value}");
}

fn error(error: ChamferError, selected: &[EdgeKey]) -> (usize, usize) {
    match error {
        ChamferError::EdgeOutsideBaseFace(edge) => (1, usize::from(selected.contains(&edge))),
        ChamferError::DistanceTooLargeOrInteracting => (2, 0),
        ChamferError::UnsupportedBodySurface => (3, 0),
        ChamferError::InvalidResult => (4, 0),
        ChamferError::EmptySelection => (5, 0),
        ChamferError::InvalidDistance => (6, 0),
        ChamferError::UnknownEdge => (7, 0),
        ChamferError::UnknownBaseFace => (8, 0),
        ChamferError::InvalidBodyTopology => (9, 0),
        ChamferError::UnsupportedBodyTopology => (10, 0),
        ChamferError::UnsupportedEdgeCurve(edge) => (11, usize::from(selected.contains(&edge))),
        ChamferError::NonManifoldEdge(edge) => (12, usize::from(selected.contains(&edge))),
        ChamferError::UnsupportedAdjacentSurface(edge) => (13, usize::from(selected.contains(&edge))),
        ChamferError::DegenerateGeometry(edge) => (14, usize::from(selected.contains(&edge))),
        ChamferError::NonConvexBody => (15, 0),
        ChamferError::UnsupportedExistingFillet => (16, 0),
    }
}

fn fillet_error(error: FilletError, selected: &[EdgeKey]) -> (usize, usize) {
    match error {
        FilletError::EmptySelection => (1, 0),
        FilletError::InvalidRadius => (2, 0),
        FilletError::UnknownEdge => (3, 0),
        FilletError::InvalidBodyTopology => (4, 0),
        FilletError::UnsupportedBodyTopology => (5, 0),
        FilletError::UnsupportedEdgeCurve(edge) => (6, usize::from(selected.contains(&edge))),
        FilletError::NonManifoldEdge(edge) => (7, usize::from(selected.contains(&edge))),
        FilletError::UnsupportedAdjacentSurface(edge) => (8, usize::from(selected.contains(&edge))),
        FilletError::DegenerateGeometry(edge) => (9, usize::from(selected.contains(&edge))),
        FilletError::AdjacentSelections(first, second) => (
            10,
            usize::from(selected.contains(&first) || selected.contains(&second)),
        ),
        FilletError::UnsupportedBodySurface => (11, 0),
        FilletError::NonConvexBody => (12, 0),
        FilletError::UnsupportedExistingFillet => (13, 0),
        FilletError::RadiusTooLargeOrInteracting => (14, 0),
        FilletError::UnsupportedEndCondition(edge) => (15, usize::from(selected.contains(&edge))),
        FilletError::InvalidResult => (16, 0),
    }
}

fn emit(body: &Body) {
    exact(body.vertices.len());
    exact(body.edges.len());
    exact(body.faces.len());
    exact(body.euler_characteristic() as usize);
    exact(body.validate().len());
    number(body.worst_vertex_gap());
    let mut curve_counts = [0usize; 5];
    for (_, curve) in body.curves.iter() {
        curve_counts[match curve {
            Curve3::Line(_) => 0,
            Curve3::Circle(_) => 1,
            Curve3::Ellipse(_) => 2,
            Curve3::PlanarSpline { .. } => 3,
            Curve3::Nurbs(_) => 4,
        }] += 1;
    }
    for count in curve_counts { exact(count); }
    let mut surface_counts = [0usize; 6];
    for (_, surface) in body.surfaces.iter() {
        surface_counts[match surface {
            Surface::Plane(_) => 0,
            Surface::Cylinder(_) => 1,
            Surface::Cone(_) => 2,
            Surface::Sphere(_) => 3,
            Surface::Torus(_) => 4,
            Surface::Nurbs(_) => 5,
        }] += 1;
    }
    for count in surface_counts { exact(count); }
    let mut points = body.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    points.sort_by(|left, right| {
        left[0].partial_cmp(&right[0]).unwrap()
            .then(left[1].partial_cmp(&right[1]).unwrap())
            .then(left[2].partial_cmp(&right[2]).unwrap())
    });
    exact(points.len());
    for point in points {
        number(point[0]);
        number(point[1]);
        number(point[2]);
    }
}

fn sheet(origin: [f64; 3], first: f64, second: f64, height: f64) -> Option<Body> {
    let profile = vec![
        Curve::Line(Line { start: [0.0, 0.0], end: [first, 0.0] }),
        Curve::Line(Line { start: [first, 0.0], end: [first, second] }),
    ];
    let plane = Plane::from_axes(origin, [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    extrude_surface(plane, &profile, [0.0, 0.0, height])
}

fn face(body: &Body, edge: EdgeKey) -> cadkernel::brep::FaceKey {
    let coedge = body.edges.get(edge).unwrap().coedges[0];
    let ring = body.coedges.get(coedge).unwrap().owner;
    body.loops.get(ring).unwrap().owner
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let origin = [values[5], values[6], values[7]];
    let mut body = match values[0] as i32 {
        0 => make::wedge(origin, values[1], values[2], values[3]),
        1 => make::pyramid(origin, values[1], values[2], values[3] as usize),
        2 => make::pyramid_frustum(origin, values[1], values[2], values[3], values[4] as usize),
        3 => sheet(origin, values[1], values[2], values[3]),
        4 => make::cuboid(origin, [values[1], values[2], values[3]]),
        _ => None,
    }
    .unwrap_or_default();
    let operation = values[15] as i32;
    if operation == 4 || operation >= 5 {
        let first_edges = body.edge_keys().collect::<Vec<_>>();
        let first = first_edges[(values[4].abs() as usize) % first_edges.len()];
        let base = face(&body, first);
        body = chamfer_edges(&body, &[first], base, values[14], values[14] * (2.0 / 3.0))
            .expect("controlled first chamfer");
        if operation >= 5 {
            let corner_edges = body.edge_keys().collect::<Vec<_>>();
            body = fillet_edges(
                &body,
                &[corner_edges[2], corner_edges[3], corner_edges[13]],
                values[14] * 0.5,
            )
            .expect("controlled corner fillet");
        }
    } else if operation >= 2 {
        let first_edges = body.edge_keys().collect::<Vec<_>>();
        let first = first_edges[(values[4].abs() as usize) % first_edges.len()];
        body = fillet_edges(&body, &[first], values[14]).expect("controlled first fillet");
    }
    let edges = body.edge_keys().collect::<Vec<_>>();
    let faces = body.face_keys().collect::<Vec<_>>();
    let selection_count = values[8] as usize;
    let selected = (0..selection_count)
        .filter_map(|index| {
            (!edges.is_empty()).then(|| edges[(values[9 + index].abs() as usize) % edges.len()])
        })
        .collect::<Vec<_>>();
    let base_face = if faces.is_empty() {
        None
    } else {
        Some(faces[(values[12].abs() as usize) % faces.len()])
    };
    if operation == 0 || operation == 3 || operation == 6 {
        let other_distance = if operation == 6 { values[13] } else { values[14] };
        let result = base_face
            .ok_or(ChamferError::UnknownBaseFace)
            .and_then(|base| chamfer_edges(&body, &selected, base, values[13], other_distance));
        match result {
            Ok(output) => {
                exact(1);
                exact(0);
                exact(0);
                emit(&output);
            }
            Err(problem) => {
                let (kind, belongs) = error(problem, &selected);
                exact(0);
                exact(kind);
                exact(belongs);
            }
        }
    } else {
        match fillet_edges(&body, &selected, values[13]) {
            Ok(output) => {
                exact(1);
                exact(0);
                exact(0);
                emit(&output);
            }
            Err(problem) => {
                let (kind, belongs) = fillet_error(problem, &selected);
                exact(0);
                exact(kind);
                exact(belongs);
            }
        }
    }
}
