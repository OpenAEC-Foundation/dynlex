// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{make, mesh, Body};
use cadkernel::space::Vec3;
use std::collections::BTreeSet;

fn number(value: f64) {
    println!("{value:.17e}");
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let shape = values[0] as i32;
    let origin = [values[1], values[2], values[3]];
    let body = match shape {
        0 => make::cuboid(origin, [values[4], values[5], values[6]]).unwrap_or_default(),
        1 => make::cylinder(origin, values[4], values[5]).unwrap_or_default(),
        2 => make::sphere(origin, values[4]).unwrap_or_default(),
        4 => make::cone(origin, values[4], values[5]).unwrap_or_default(),
        5 => make::torus(origin, values[4], values[5]).unwrap_or_default(),
        6 => make::frustum(origin, values[4], values[5], values[6], values[12]).unwrap_or_default(),
        7 => make::wedge(origin, values[4], values[5], values[6]).unwrap_or_default(),
        8 => make::pyramid(origin, values[4], values[5], values[6] as usize).unwrap_or_default(),
        _ => Body::new(),
    };
    let result = mesh::body(&body, values[7], values[8]);
    number(result.triangles.len() as f64);
    number(result.positions.len() as f64);
    number(result.surface_area().unwrap_or(0.0));
    let surface_centroid = result
        .surface_properties()
        .map(|(_, centroid)| centroid)
        .unwrap_or([0.0; 3]);
    for value in surface_centroid {
        number(value);
    }
    let mut minimum_winding = f64::INFINITY;
    for triangle in &result.triangles {
        let a = Vec3::from(result.positions[triangle[0]]);
        let b = Vec3::from(result.positions[triangle[1]]);
        let c = Vec3::from(result.positions[triangle[2]]);
        let ab = b - a;
        let ac = c - a;
        let edge_scale = ab.length().max(ac.length()).max((c - b).length());
        let cross = ab.cross(ac);
        if cross.length() > f64::EPSILON * 128.0 * edge_scale * edge_scale {
            if let Some(wound) = cross.normalize() {
                minimum_winding = minimum_winding.min(wound.dot(Vec3::from(result.normals[triangle[0]])));
            }
        }
    }
    number(if minimum_winding.is_finite() { minimum_winding } else { 0.0 });
    let mut low = [f64::INFINITY; 3];
    let mut high = [f64::NEG_INFINITY; 3];
    for point in &result.positions {
        for axis in 0..3 {
            low[axis] = low[axis].min(point[axis]);
            high[axis] = high[axis].max(point[axis]);
        }
    }
    if result.positions.is_empty() {
        low = [0.0; 3];
        high = [0.0; 3];
    }
    for value in low.into_iter().chain(high) {
        number(value);
    }
    let angles = result
        .positions
        .iter()
        .map(|point| (point[1] - origin[1]).atan2(point[0] - origin[0]))
        .map(|angle| (angle * 1e6) as i64)
        .collect::<BTreeSet<_>>();
    number(angles.len() as f64);
    if let Some(properties) = result.inertial_properties() {
        number(1.0);
        number(properties.volume);
        for value in properties.centroid {
            number(value);
        }
        for value in properties.principal_moments {
            number(value);
        }
    } else {
        for _ in 0..8 {
            number(0.0);
        }
    }
    let mut tolerance = mesh::TessellationTolerance::new(values[7], values[8]);
    if values.get(9).copied().unwrap_or(0.0) > 0.0 {
        tolerance = tolerance.with_chordal_deflection(values[9]);
    }
    tolerance = tolerance.with_uv_isolines(
        values.get(10).copied().unwrap_or(0.0).max(0.0) as usize,
        values.get(11).copied().unwrap_or(0.0).max(0.0) as usize,
    );
    let display = mesh::tessellate(&body, tolerance);
    number(display.mesh.triangles.len() as f64);
    number(display.triangle_faces.len() as f64);
    number(display.edges.len() as f64);
    number(display.edges.iter().map(|edge| edge.positions.len()).sum::<usize>() as f64);
    number(display.isolines.len() as f64);
    number(display.missing_faces.len() as f64);
    number(display.is_complete() as u8 as f64);
    let wire = mesh::tessellate_wireframe(&body, tolerance);
    number(wire.edges.len() as f64);
    number(wire.edges.iter().map(|edge| edge.positions.len()).sum::<usize>() as f64);
    number(wire.isolines.len() as f64);
    number(wire.missing_faces.len() as f64);
    let contour = mesh::silhouette(&display.silhouette_source(), [0.0, 0.0, 1.0]);
    number(contour.len() as f64);
    let mut contour_low = [f64::INFINITY; 3];
    let mut contour_high = [f64::NEG_INFINITY; 3];
    for point in &contour {
        for axis in 0..3 {
            contour_low[axis] = contour_low[axis].min(point[axis]);
            contour_high[axis] = contour_high[axis].max(point[axis]);
        }
    }
    if contour.is_empty() {
        contour_low = [0.0; 3];
        contour_high = [0.0; 3];
    }
    for value in contour_low.into_iter().chain(contour_high) {
        number(value);
    }
    number(display.precision);
}
