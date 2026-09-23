// SPDX-License-Identifier: MPL-2.0
// Link the two unmodified pinned source modules as standalone Rust libraries.
extern crate cadkernel_vector2;
extern crate cadkernel_vector3;
use cadkernel_vector2::Vec2;
use cadkernel_vector3::Vec3;

fn emit(value: f64) { println!("{:.17e}", value); }
fn emit2(value: Vec2) { emit(value.x); emit(value.y); }
fn emit3(value: Vec3) { emit(value.x); emit(value.y); emit(value.z); }

fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    assert_eq!(args.len(), 10);
    let a2 = Vec2::new(args[0], args[1]);
    let b2 = Vec2::new(args[3], args[4]);
    let p2 = Vec2::new(args[6], args[7]);
    let a3 = Vec3::new(args[0], args[1], args[2]);
    let b3 = Vec3::new(args[3], args[4], args[5]);
    let p3 = Vec3::new(args[6], args[7], args[8]);
    let t = args[9];
    emit2(a2 + b2);
    emit2(a2 - b2);
    emit2(a2 * t);
    emit2(a2 / t);
    emit2(-a2);
    emit(a2.dot(b2));
    emit(a2.cross(b2));
    emit(a2.length_squared());
    emit(a2.length());
    emit(a2.distance(b2));
    emit(a2.distance_squared(b2));
    emit2(a2.lerp(b2, t));
    emit(p2.distance_to_segment(a2, b2));
    emit2(a2.perpendicular());
    emit(a2.angle());
    let norm2 = a2.normalize();
    emit(norm2.is_some() as u8 as f64);
    emit2(norm2.unwrap_or(Vec2::ZERO));
    emit3(a3 + b3);
    emit3(a3 - b3);
    emit3(a3 * t);
    emit3(a3 / t);
    emit3(-a3);
    emit(a3.dot(b3));
    emit3(a3.cross(b3));
    emit(a3.length_squared());
    emit(a3.length());
    emit(a3.distance(b3));
    emit(a3.distance_squared(b3));
    emit3(a3.lerp(b3, t));
    emit(p3.distance_to_segment(a3, b3));
    let norm3 = a3.normalize();
    emit(norm3.is_some() as u8 as f64);
    emit3(norm3.unwrap_or(Vec3::ZERO));
    emit(a3.is_finite() as u8 as f64);
    emit(a3.is_parallel_to(b3, t) as u8 as f64);
}
