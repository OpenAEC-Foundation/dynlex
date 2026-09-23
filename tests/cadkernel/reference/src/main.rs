// SPDX-License-Identifier: MPL-2.0
// Numerical fixtures for https://github.com/HakanSeven12/cadkernel
// at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f (MPL-2.0).
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// This harness calls upstream APIs; it contains no copied kernel implementation.

use cadkernel::brep::{body_bounds, make::cuboid, mesh};
use cadkernel::geom2d::{angle_within_arc, arc_parameter, arc_span, normalize_angle, Frame, Vec2};
use cadkernel::space::{Plane, Vec3};
use std::f64::consts::{FRAC_PI_2, PI, TAU};

const ABSOLUTE_TOLERANCE: f64 = 1e-10;

#[derive(Default)]
struct Results {
    lines: Vec<String>,
}

impl Results {
    fn scalar(&mut self, label: &str, actual: f64, expected: f64) {
        self.vector(label, [actual], [expected]);
    }

    fn vector<const N: usize>(&mut self, label: &str, actual: [f64; N], expected: [f64; N]) {
        for (axis, (actual, expected)) in actual.iter().zip(expected).enumerate() {
            assert!(
                actual.is_finite() && (actual - expected).abs() <= ABSOLUTE_TOLERANCE,
                "{label}[{axis}]: actual={actual:.17e}, expected={expected:.17e}"
            );
        }
        let value = actual.map(|value| format!("{value:.17e}")).join(",");
        self.lines.push(format!("{label}={value}"));
    }

    fn boolean(&mut self, label: &str, actual: bool, expected: bool) {
        assert_eq!(actual, expected, "{label}");
        self.lines.push(format!("{label}={actual}"));
    }

    fn integer(&mut self, label: &str, actual: usize, expected: usize) {
        assert_eq!(actual, expected, "{label}");
        self.lines.push(format!("{label}={actual}"));
    }

    fn print(self) {
        println!("reference.version=1");
        println!("reference.upstream=953d546b68aef4b6692566a1a9b077fc5bd9fb4f");
        println!("{}", self.lines.join("\n"));
        println!("reference.checks={}", self.lines.len());
        println!("reference.status=ok");
    }
}

fn vectors(results: &mut Results) {
    let a = Vec2::new(3.0, 4.0);
    let b = Vec2::new(-2.0, 5.0);
    results.vector("vec2.add", (a + b).to_array(), [1.0, 9.0]);
    results.vector("vec2.subtract", (a - b).to_array(), [5.0, -1.0]);
    results.vector("vec2.scale", (a * 2.0).to_array(), [6.0, 8.0]);
    results.vector("vec2.divide", (a / 2.0).to_array(), [1.5, 2.0]);
    results.vector("vec2.negate", (-a).to_array(), [-3.0, -4.0]);
    results.scalar("vec2.dot", a.dot(b), 14.0);
    results.scalar("vec2.cross", a.cross(b), 23.0);
    results.scalar("vec2.length_squared", a.length_squared(), 25.0);
    results.scalar("vec2.length", a.length(), 5.0);
    results.vector(
        "vec2.normalize",
        a.normalize().expect("vec2 direction").to_array(),
        [0.6, 0.8],
    );
    results.vector(
        "vec2.perpendicular",
        a.perpendicular().to_array(),
        [-4.0, 3.0],
    );
    results.vector("vec2.lerp", a.lerp(b, 0.25).to_array(), [1.75, 4.25]);
    results.scalar(
        "vec2.segment_interior",
        a.distance_to_segment(Vec2::ZERO, Vec2::new(6.0, 0.0)),
        4.0,
    );
    results.scalar(
        "vec2.segment_endpoint",
        Vec2::new(9.0, 4.0).distance_to_segment(Vec2::ZERO, Vec2::new(6.0, 0.0)),
        5.0,
    );
    results.scalar(
        "vec2.segment_collapsed",
        a.distance_to_segment(Vec2::ZERO, Vec2::ZERO),
        5.0,
    );
    results.boolean("vec2.zero_refused", Vec2::ZERO.normalize().is_none(), true);
    results.boolean(
        "vec2.tiny_refused",
        Vec2::new(1e-200, 0.0).normalize().is_none(),
        true,
    );

    let a = Vec3::new(2.0, -3.0, 6.0);
    let b = Vec3::new(-1.0, 4.0, 2.0);
    results.vector("vec3.add", (a + b).to_array(), [1.0, 1.0, 8.0]);
    results.vector("vec3.subtract", (a - b).to_array(), [3.0, -7.0, 4.0]);
    results.vector("vec3.scale", (a * 2.0).to_array(), [4.0, -6.0, 12.0]);
    results.vector("vec3.divide", (a / 2.0).to_array(), [1.0, -1.5, 3.0]);
    results.vector("vec3.negate", (-a).to_array(), [-2.0, 3.0, -6.0]);
    results.scalar("vec3.dot", a.dot(b), -2.0);
    results.vector("vec3.cross", a.cross(b).to_array(), [-30.0, -10.0, 5.0]);
    results.scalar("vec3.length_squared", a.length_squared(), 49.0);
    results.scalar("vec3.length", a.length(), 7.0);
    results.vector(
        "vec3.normalize",
        a.normalize().expect("vec3 direction").to_array(),
        [2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0],
    );
    results.vector("vec3.lerp", a.lerp(b, 0.5).to_array(), [0.5, 0.5, 4.0]);
    results.scalar(
        "vec3.segment_collapsed",
        a.distance_to_segment(Vec3::ZERO, Vec3::ZERO),
        7.0,
    );
    results.boolean("vec3.zero_refused", Vec3::ZERO.normalize().is_none(), true);
    results.boolean(
        "vec3.tiny_refused",
        Vec3::new(1e-200, 0.0, 0.0).normalize().is_none(),
        true,
    );
    results.boolean("vec3.antiparallel", a.is_parallel_to(-a, 1e-12), true);
    results.boolean(
        "vec3.zero_parallel",
        a.is_parallel_to(Vec3::ZERO, 1e-12),
        false,
    );
    results.boolean(
        "vec3.infinity_finite",
        Vec3::new(f64::INFINITY, 0.0, 0.0).is_finite(),
        false,
    );
}

fn angles(results: &mut Results) {
    results.scalar(
        "angle.normalize_negative",
        normalize_angle(-FRAC_PI_2),
        1.5 * PI,
    );
    results.scalar("angle.span_wrapped", arc_span(1.5 * PI, FRAC_PI_2), PI);
    results.scalar("angle.span_equal", arc_span(0.0, 0.0), TAU);
    results.scalar(
        "angle.parameter_midpoint",
        arc_parameter(0.0, 1.5 * PI, FRAC_PI_2),
        0.5,
    );
    results.boolean(
        "angle.contains_wrapped",
        angle_within_arc(0.0, 1.5 * PI, FRAC_PI_2),
        true,
    );
    results.boolean(
        "angle.excludes_opposite",
        angle_within_arc(PI, 1.5 * PI, FRAC_PI_2),
        false,
    );
    results.boolean(
        "angle.contains_endpoint",
        angle_within_arc(FRAC_PI_2, 1.5 * PI, FRAC_PI_2),
        true,
    );
    results.boolean(
        "angle.equal_is_full_turn",
        angle_within_arc(PI, 0.0, 0.0),
        true,
    );
}

fn frames_and_planes(results: &mut Results) {
    let points = [
        [1_200_000.0, -4_500_000.0, 8.0],
        [1_200_008.0, -4_499_994.0, 12.0],
    ];
    let frame = Frame::around(&points);
    results.vector(
        "frame.origin",
        frame.origin(),
        [1_200_004.0, -4_499_997.0, 10.0],
    );
    results.vector("frame.lift", frame.lift(points[0]), [-4.0, -3.0, -2.0]);
    results.vector("frame.lower", frame.lower([4.0, 3.0, 2.0]), points[1]);
    results.vector(
        "frame.lift_2d",
        frame.lift_2d([1_200_000.0, -4_500_000.0]),
        [-4.0, -3.0],
    );
    results.vector(
        "frame.lower_2d",
        frame.lower_2d([4.0, 3.0]),
        [1_200_008.0, -4_499_994.0],
    );
    let empty: [[f64; 3]; 0] = [];
    results.vector(
        "frame.empty_origin",
        Frame::around(&empty).origin(),
        [0.0; 3],
    );

    let plane = Plane::from_axes([10.0, 20.0, 30.0], [2.0, 0.0, 0.0], [1.0, 3.0, 0.0]);
    results.vector(
        "plane.point",
        plane.point_at([2.0, -1.0]),
        [13.0, 17.0, 30.0],
    );
    results.vector(
        "plane.project",
        plane.project([13.0, 17.0, 35.0]).expect("plane projection"),
        [2.0, -1.0],
    );
    results.vector(
        "plane.normal",
        plane.normal().expect("plane normal"),
        [0.0, 0.0, 1.0],
    );
    results.scalar(
        "plane.distance",
        plane
            .distance_to([13.0, 17.0, 35.0])
            .expect("plane distance"),
        5.0,
    );
    results.boolean("plane.sheared_orthonormal", plane.is_orthonormal(), false);
    results.boolean(
        "plane.contains",
        plane.contains([13.0, 17.0, 30.0], 1e-9),
        true,
    );
    let reversed = Plane::from_axes(plane.origin, plane.x_axis, [-1.0, -3.0, 0.0]);
    results.scalar(
        "plane.reversed_distance",
        reversed
            .distance_to([13.0, 17.0, 35.0])
            .expect("reversed plane"),
        -5.0,
    );
    let collapsed = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [2.0, 0.0, 0.0]);
    results.boolean(
        "plane.collapsed_normal_refused",
        collapsed.normal().is_none(),
        true,
    );
    results.boolean(
        "plane.collapsed_project_refused",
        collapsed.project([0.0; 3]).is_none(),
        true,
    );
    let orthogonal =
        Plane::orthonormal([0.0; 3], [2.0, 0.0, 1.0], [0.0, 0.0, 2.0]).expect("orthonormal plane");
    results.vector("plane.orthonormal_x", orthogonal.x_axis, [1.0, 0.0, 0.0]);
    results.vector("plane.orthonormal_y", orthogonal.y_axis, [0.0, 1.0, 0.0]);
}

fn boxes(results: &mut Results) {
    let body = cuboid([-1.0, -1.5, -2.0], [2.0, 3.0, 4.0]).expect("centered box");
    results.integer("box.vertices", body.vertices.len(), 8);
    results.integer("box.edges", body.edges.len(), 12);
    results.integer("box.coedges", body.coedges.len(), 24);
    results.integer("box.loops", body.loops.len(), 6);
    results.integer("box.faces", body.faces.len(), 6);
    results.integer("box.shells", body.shells.len(), 1);
    results.integer("box.lumps", body.lumps.len(), 1);
    results.integer("box.validation_flaws", body.validate().len(), 0);
    results.integer(
        "box.euler",
        body.vertices.len() + body.faces.len() - body.edges.len(),
        2,
    );
    let bounds = body_bounds(&body).expect("box bounds");
    results.vector("box.bounds_min", bounds.min, [-1.0, -1.5, -2.0]);
    results.vector("box.bounds_max", bounds.max, [1.0, 1.5, 2.0]);
    let mesh = mesh::body(&body, PI / 24.0, 1e-9);
    let mass = mesh.inertial_properties().expect("box inertial properties");
    results.scalar("box.volume", mass.volume, 24.0);
    results.vector("box.centroid", mass.centroid, [0.0; 3]);
    results.scalar(
        "box.surface_area",
        mesh.surface_area().expect("box area"),
        52.0,
    );
    results.vector("box.inertia", mass.moment_of_inertia, [50.0, 40.0, 26.0]);

    let survey = cuboid([1_200_000.0, -4_500_000.0, 10.0], [2.0, 3.0, 4.0]).expect("survey box");
    results.integer("box.survey_validation_flaws", survey.validate().len(), 0);
    let survey_mesh = mesh::body(&survey, PI / 24.0, 1e-9);
    let (volume, centroid) = survey_mesh.mass_properties().expect("survey box mass");
    results.scalar("box.survey_volume", volume, 24.0);
    results.vector(
        "box.survey_centroid",
        centroid,
        [1_200_001.0, -4_499_998.5, 12.0],
    );
    results.scalar(
        "box.survey_surface_area",
        survey_mesh.surface_area().expect("survey box area"),
        52.0,
    );
    results.boolean(
        "box.zero_size_refused",
        cuboid([0.0; 3], [0.0, 3.0, 4.0]).is_none(),
        true,
    );
    results.boolean(
        "box.negative_size_refused",
        cuboid([0.0; 3], [-2.0, 3.0, 4.0]).is_none(),
        true,
    );
}

fn main() {
    assert!(
        std::env::args_os().len() == 1,
        "usage: cadkernel-reference (no arguments)"
    );
    let mut results = Results::default();
    vectors(&mut results);
    angles(&mut results);
    frames_and_planes(&mut results);
    boxes(&mut results);
    results.print();
}
