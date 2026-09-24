// SPDX-License-Identifier: MPL-2.0
// Transport formulas copied from pinned cadkernel src/brep/sweep_path.rs.
use cadkernel::space::Vec3;
use std::f64::consts::PI;

#[derive(Clone, Copy)]
struct Frame { origin: Vec3, x: Vec3, y: Vec3 }

fn rotate(value: Vec3, axis: Vec3, angle: f64) -> Vec3 {
    let (sin, cos) = angle.sin_cos();
    value * cos + axis.cross(value) * sin + axis * axis.dot(value) * (1.0 - cos)
}

fn transport(value: Vec3, from: Vec3, to: Vec3) -> Option<Vec3> {
    let cross = from.cross(to);
    let sin = cross.length();
    let cos = from.dot(to).clamp(-1.0, 1.0);
    if sin <= 1e-12 {
        if cos >= 0.0 { return Some(value); }
        let axis = if from.x.abs() < 0.8 { from.cross(Vec3::X) } else { from.cross(Vec3::Y) }.normalize()?;
        return Some(rotate(value, axis, PI));
    }
    Some(rotate(value, cross / sin, sin.atan2(cos)))
}

fn moved(frame: Frame, from: Vec3, to: Vec3, old_point: Vec3, point: Vec3) -> Option<Frame> {
    Some(Frame { origin: point + transport(frame.origin - old_point, from, to)?,
        x: transport(frame.x, from, to)?, y: transport(frame.y, from, to)? })
}

fn miter(frame: Frame, point: Vec3, tangent: Vec3, other: Vec3) -> Option<Frame> {
    let normal = (tangent + other).normalize()?;
    let dot = normal.dot(tangent);
    if dot <= 1e-6 { return None; }
    let cut = |v: Vec3| v - tangent * (v.dot(normal) / dot);
    Some(Frame { origin: point + cut(frame.origin - point), x: cut(frame.x), y: cut(frame.y) })
}

fn twist_frame(frame: Frame, point: Vec3, angle: f64, scale: f64) -> Option<Frame> {
    let normal = frame.x.cross(frame.y).normalize()?;
    Some(Frame { origin: point + rotate(frame.origin - point, normal, angle) * scale,
        x: rotate(frame.x, normal, angle) * scale, y: rotate(frame.y, normal, angle) * scale })
}

fn emit(value: f64) { println!("{value:.17e}"); }
fn emit_vector(value: Option<Vec3>) {
    match value {
        None => emit(0.0),
        Some(v) => { emit(1.0); emit(v.x); emit(v.y); emit(v.z); }
    }
}
fn emit_frame(value: Option<Frame>) {
    match value {
        None => emit(0.0),
        Some(v) => {
            emit(1.0);
            for point in [v.origin, v.x, v.y] { emit(point.x); emit(point.y); emit(point.z); }
        }
    }
}

fn main() {
    let x = Vec3::X;
    let y = Vec3::Y;
    let value = Vec3::new(0.2, -0.4, 0.6);
    let diagonal = Vec3::new(1.0, 1.0, 0.0).normalize().unwrap();
    let spatial = Vec3::new(-0.5, 0.5, 1.0).normalize().unwrap();
    let frame = Frame { origin: Vec3::new(1.0, 2.0, 3.0), x: y, y: Vec3::Z };
    emit_vector(transport(value, x, x));
    emit_vector(transport(value, x, -x));
    emit_vector(transport(value, y, -y));
    emit_vector(transport(value, x, y));
    emit_vector(transport(value, diagonal, spatial));
    emit_vector(transport(value, x, Vec3::new(-1.0, 1e-13, 0.0)));
    emit_frame(moved(frame, x, y, Vec3::ZERO, Vec3::new(0.0, 3.0, 0.0)));
    emit_frame(miter(frame, Vec3::ZERO, x, y));
    emit_frame(miter(frame, Vec3::ZERO, x, -x));
    emit_frame(twist_frame(frame, Vec3::new(0.5, 0.5, 0.5), 0.8, 1.5));
    emit_frame(twist_frame(Frame { origin: Vec3::ZERO, x, y: x }, Vec3::ZERO, 0.8, 1.5));
}
