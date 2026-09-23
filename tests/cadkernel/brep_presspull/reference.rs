// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{make, planar_region, presspull_face, Body, Curve3, PresspullMode, Surface};
use cadkernel::geom2d::{Circle, Curve, Ellipse, EllipseArc, Line};
use cadkernel::space::Plane;

fn number(value: f64) { println!("{value:.17}"); }
fn exact(value: usize) { println!("{value}"); }

fn emit(body: &Body) {
    exact(body.vertices.len());
    exact(body.edges.len());
    exact(body.faces.len());
    println!("{}", body.euler_characteristic());
    exact(body.validate().len());
    number(body.worst_vertex_gap());

    let mut curve_counts = [0usize; 5];
    for (_, edge) in body.edges.iter() {
        let Some(curve) = body.curves.get(edge.curve) else { continue };
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

    let mut points = body.vertices.iter().map(|(_, vertex)| vertex.point).collect::<Vec<_>>();
    points.sort_by(|left, right| {
        left[0].total_cmp(&right[0])
            .then(left[1].total_cmp(&right[1]))
            .then(left[2].total_cmp(&right[2]))
    });
    exact(points.len());
    for point in points {
        number(point[0]);
        number(point[1]);
        number(point[2]);
    }
}

fn rectangle(min: [f64; 2], max: [f64; 2]) -> Vec<Curve> {
    let corners = [
        [min[0], min[1]], [max[0], min[1]],
        [max[0], max[1]], [min[0], max[1]],
    ];
    (0..4).map(|index| Curve::Line(Line {
        start: corners[index], end: corners[(index + 1) % 4],
    })).collect()
}

fn region(values: &[f64]) -> Option<Body> {
    let plane = Plane::from_axes(
        [values[5], values[6], values[7]],
        [1.0, 0.0, 0.0], [0.0, 1.0, 0.0],
    );
    let loops = match values[0] as i32 {
        10 => vec![rectangle([0.0, 0.0], [values[1], values[2]])],
        11 => vec![vec![Curve::Circle(Circle { centre: [0.0, 0.0], radius: values[1] })]],
        12 => vec![vec![Curve::Ellipse(EllipseArc {
            ellipse: Ellipse {
                centre: [0.0, 0.0], major_radius: values[1], minor_radius: values[2],
                major_axis: [1.0, 0.0],
            },
            start_parameter: 0.0,
            end_parameter: std::f64::consts::TAU,
        })]],
        13 => vec![
            rectangle([0.0, 0.0], [values[1], values[2]]),
            rectangle([values[3], values[3]], [values[1] - values[3], values[2] - values[3]]),
        ],
        _ => return None,
    };
    planar_region(plane, &loops)
}

fn main() {
    let values = std::env::args().skip(1).map(|value| value.parse::<f64>().unwrap()).collect::<Vec<_>>();
    let origin = [values[5], values[6], values[7]];
    if values[0] >= 10.0 {
        if let Some(output) = region(&values) {
            exact(1);
            emit(&output);
        } else {
            exact(0);
        }
        return;
    }
    let body = match values[0] as i32 {
        0 => make::cuboid(origin, [values[1], values[2], values[3]]),
        1 => make::wedge(origin, values[1], values[2], values[3]),
        2 => make::pyramid(origin, values[1], values[2], values[3] as usize),
        3 => make::pyramid_frustum(origin, values[1], values[2], values[3], values[4] as usize),
        4 => make::cylinder(origin, values[1], values[2]),
        _ => None,
    }.unwrap_or_default();
    let faces = body.face_keys().collect::<Vec<_>>();
    let mode = if values[10] == 0.0 { PresspullMode::Extrude } else { PresspullMode::Offset };
    let result = (!faces.is_empty())
        .then(|| presspull_face(&body, faces[(values[8].abs() as usize) % faces.len()], values[9], mode))
        .flatten();
    if let Some(output) = result {
        exact(1);
        emit(&output);
    } else {
        exact(0);
    }
}
