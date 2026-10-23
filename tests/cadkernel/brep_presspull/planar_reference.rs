// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{
    intersect_planar_regions, planar_region, subtract_planar_regions,
    union_planar_regions, Body, Curve3, PlanarIntersection, Snag, Surface,
};
use cadkernel::geom2d::{Circle, Curve, Ellipse, EllipseArc, Line};
use cadkernel::space::Plane;

fn number(value: f64) { println!("{value:.17e}"); }
fn exact(value: usize) { println!("d {value}"); }
fn flag(value: bool) { exact(usize::from(value)); }

fn point(value: [f64; 3]) {
    for coordinate in value { number(coordinate); }
}

fn emit(body: &Body) {
    for count in [
        body.roots.len(), body.vertices.len(), body.edges.len(), body.coedges.len(),
        body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
        body.curves.len(), body.surfaces.len(),
    ] { exact(count); }
    println!("d {}", body.euler_characteristic());
    exact(body.validate().len());
    number(body.worst_vertex_gap());

    let mut curve_counts = [0usize; 5];
    let mut samples = Vec::new();
    for (_, edge) in body.edges.iter() {
        let Some(curve) = body.curves.get(edge.curve) else { continue };
        curve_counts[match curve {
            Curve3::Line(_) => 0,
            Curve3::Circle(_) => 1,
            Curve3::Ellipse(_) => 2,
            Curve3::PlanarSpline { .. } => 3,
            Curve3::Nurbs(_) => 4,
        }] += 1;
        for step in 0..=4 {
            let fraction = step as f64 / 4.0;
            samples.push(curve.point_at(
                edge.start_parameter + fraction * (edge.end_parameter - edge.start_parameter),
            ));
        }
    }
    for count in curve_counts { exact(count); }

    let mut surface_counts = [0usize; 6];
    for (_, face) in body.faces.iter() {
        let Some(surface) = body.surfaces.get(face.surface) else { continue };
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

    let sort_points = |values: &mut Vec<[f64; 3]>| values.sort_by(|left, right| {
        left[0].total_cmp(&right[0])
            .then(left[1].total_cmp(&right[1]))
            .then(left[2].total_cmp(&right[2]))
    });
    let mut vertices = body.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    sort_points(&mut vertices);
    sort_points(&mut samples);
    exact(vertices.len());
    for value in vertices { point(value); }
    exact(samples.len());
    for value in samples { point(value); }

    let mut loop_degrees = body.loops.iter().map(|(_, ring)| ring.coedges.len()).collect::<Vec<_>>();
    let mut face_degrees = body.faces.iter().map(|(_, face)| face.loops.len()).collect::<Vec<_>>();
    let mut shell_degrees = body.shells.iter().map(|(_, shell)| shell.faces.len()).collect::<Vec<_>>();
    let mut lump_degrees = body.lumps.iter().map(|(_, lump)| lump.shells.len()).collect::<Vec<_>>();
    for degrees in [&mut loop_degrees, &mut face_degrees, &mut shell_degrees, &mut lump_degrees] {
        degrees.sort_unstable();
        exact(degrees.len());
        for degree in degrees.iter() { exact(*degree); }
    }
}

fn rectangle_boundary(min: [f64; 2], max: [f64; 2]) -> Vec<Curve> {
    let corners = [
        [min[0], min[1]],
        [max[0], min[1]],
        [max[0], max[1]],
        [min[0], max[1]],
    ];
    (0..4)
        .map(|index| Curve::Line(Line {
            start: corners[index],
            end: corners[(index + 1) % 4],
        }))
        .collect()
}

fn region(kind: i32, values: [f64; 4], z: f64) -> Body {
    let plane = Plane::from_axes([0.0, 0.0, z], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]);
    let loops = match kind {
        1 => vec![vec![Curve::Circle(Circle {
            centre: [values[0], values[1]],
            radius: values[2],
        })]],
        2 => vec![vec![Curve::Ellipse(EllipseArc {
            ellipse: Ellipse {
                centre: [values[0], values[1]],
                major_radius: values[2],
                minor_radius: values[3],
                major_axis: [1.0, 0.0],
            },
            start_parameter: 0.0,
            end_parameter: std::f64::consts::TAU,
        })]],
        3 => {
            let margin = ((values[2] - values[0]).abs().min((values[3] - values[1]).abs())) * 0.2;
            vec![
                rectangle_boundary([values[0], values[1]], [values[2], values[3]]),
                rectangle_boundary(
                    [values[0] + margin, values[1] + margin],
                    [values[2] - margin, values[3] - margin],
                ),
            ]
        }
        _ => vec![rectangle_boundary([values[0], values[1]], [values[2], values[3]])],
    };
    planar_region(plane, &loops).unwrap()
}

fn snag(value: Snag) -> usize {
    match value {
        Snag::NoClosedForm => 1,
        Snag::Coincident => 2,
        Snag::CutRefused => 3,
    }
}

fn success(kind: usize, body: Body) {
    flag(true);
    exact(kind);
    emit(&body);
}

fn failure(error: Snag) {
    flag(false);
    exact(snag(error));
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let left = region(values[10] as i32, [values[1], values[2], values[3], values[4]], values[12]);
    let right = region(values[11] as i32, [values[5], values[6], values[7], values[8]], values[13]);
    let tolerance = values[9];
    match values[0] as i32 {
        0 => match union_planar_regions(&[left, right], tolerance) {
            Ok(body) => success(0, body),
            Err(error) => failure(error),
        },
        1 => match intersect_planar_regions(&[left, right], tolerance) {
            Ok(PlanarIntersection::Area(body)) => success(0, body),
            Ok(PlanarIntersection::Touching) => success(1, Body::new()),
            Ok(PlanarIntersection::Disjoint) => success(2, Body::new()),
            Err(error) => failure(error),
        },
        2 => match subtract_planar_regions(&[left], &[right], tolerance) {
            Ok(body) => success(0, body),
            Err(error) => failure(error),
        },
        _ => unreachable!(),
    }
}
