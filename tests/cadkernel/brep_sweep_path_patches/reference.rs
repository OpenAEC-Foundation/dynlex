// SPDX-License-Identifier: MPL-2.0
// Isolated formulas from pinned src/brep/sweep_path.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::geom2d::{Arc, Curve};
use cadkernel::space::{NurbsCurve3, Plane, Vec3};
use std::f64::consts::PI;

#[derive(Clone, Copy)]
struct Frame { origin: Vec3, x: Vec3, y: Vec3 }

impl Frame {
    fn plus(self, other: Self) -> Self {
        Self { origin: self.origin + other.origin, x: self.x + other.x, y: self.y + other.y }
    }
    fn minus(self, other: Self) -> Self { self.plus(other.times(-1.0)) }
    fn times(self, t: f64) -> Self {
        Self { origin: self.origin * t, x: self.x * t, y: self.y * t }
    }
    fn lerp(self, other: Self, t: f64) -> Self { self.plus(other.minus(self).times(t)) }
    fn point(self, p: [f64; 2]) -> Vec3 { self.origin + self.x * p[0] + self.y * p[1] }
}

fn frame_error(a: Frame, b: Frame, radius: f64) -> f64 {
    a.origin.distance(b.origin) + radius * (a.x.distance(b.x) + a.y.distance(b.y))
}

#[derive(Clone)]
enum Piece {
    Line(Vec3, Vec3), Planar(Plane, Curve, bool), Spline(NurbsCurve3, f64, f64),
}

const GAUSS: [(f64, f64); 5] = [
    (-0.906179845938664, 0.236926885056189),
    (-0.538469310105683, 0.478628670499366),
    (0.0, 0.568888888888889),
    (0.538469310105683, 0.478628670499366),
    (0.906179845938664, 0.236926885056189),
];

impl Piece {
    fn point(&self, t: f64) -> Vec3 {
        Vec3::from(match self {
            Self::Line(a, b) => a.lerp(*b, t).to_array(),
            Self::Planar(plane, curve, forward) =>
                plane.point_at(curve.point_at(if *forward { t } else { 1.0 - t })),
            Self::Spline(curve, a, b) => curve.point_at_knot(a + (b - a) * t),
        })
    }
    fn spline_derivative(&self, t: f64) -> Vec3 {
        let a = (t - 1e-5).max(0.0);
        let b = (t + 1e-5).min(1.0);
        (self.point(b) - self.point(a)) / (b - a)
    }
    fn tangent(&self, t: f64) -> Option<Vec3> {
        let direction = match self {
            Self::Line(a, b) => *b - *a,
            Self::Planar(plane, curve, forward) =>
                Vec3::from(plane.vector_at(curve.tangent_at(if *forward { t } else { 1.0 - t })))
                    * if *forward { 1.0 } else { -1.0 },
            Self::Spline(_, _, _) => self.spline_derivative(t),
        };
        direction.is_finite().then_some(())?;
        if direction.length() > 1e-12 { return direction.normalize(); }
        (self.point((t + 1e-6).min(1.0)) - self.point((t - 1e-6).max(0.0))).normalize()
    }
    fn speed(&self, t: f64) -> f64 {
        match self {
            Self::Line(a, b) => a.distance(*b),
            Self::Planar(plane, curve, forward) => Vec3::from(plane.vector_at(
                curve.tangent_at(if *forward { t } else { 1.0 - t }))).length(),
            Self::Spline(_, _, _) => self.spline_derivative(t).length(),
        }
    }
    fn length_to(&self, end: f64) -> f64 {
        if let Self::Line(a, b) = self { return a.distance(*b) * end; }
        if let Self::Planar(plane, curve, _) = self {
            if let Curve::Line(line) = curve {
                return Vec3::from(plane.vector_at(line.direction())).length() * end;
            }
            if matches!(curve, Curve::Arc(_)) && plane.is_orthonormal() {
                return curve.length() * end;
            }
        }
        (0..16).map(|panel| GAUSS.into_iter().map(|(node, weight)| {
            let t = end * (panel as f64 + 0.5 + node * 0.5) / 16.0;
            self.speed(t) * weight * end / 32.0
        }).sum::<f64>()).sum()
    }
    fn length(&self) -> f64 { self.length_to(1.0) }
}

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
fn binormal(piece: &Piece, t: f64) -> Option<Vec3> {
    let start = piece.tangent((t - 1e-3).max(0.0))?;
    let end = piece.tangent((t + 1e-3).min(1.0))?;
    let cross = start.cross(end);
    (cross.length() > 1e-9).then(|| cross.normalize()).flatten()
}
fn curved_transport(frame: Frame, piece: &Piece, a: f64, b: f64, bank: bool) -> Option<Frame> {
    let from = piece.tangent(a)?;
    let to = piece.tangent(b)?;
    let point = piece.point(b);
    let mut result = moved(frame, from, to, piece.point(a), point)?;
    if bank {
        if let (Some(previous), Some(next)) = (binormal(piece, a), binormal(piece, b)) {
            let previous = transport(previous, from, to)?;
            let roll = to.dot(previous.cross(next)).atan2(previous.dot(next));
            result = Frame { origin: point + rotate(result.origin - point, to, roll),
                x: rotate(result.x, to, roll), y: rotate(result.y, to, roll) };
        }
    }
    Some(result)
}
fn divide_path(piece: &Piece, a: f64, b: f64, depth: usize, result: &mut Vec<f64>) -> Option<()> {
    let middle = (a + b) * 0.5;
    let ta = piece.tangent(a)?;
    let tb = piece.tangent(b)?;
    let tm = piece.tangent(middle)?;
    let chord = piece.point(a).distance(piece.point(b));
    let broken = piece.point(a).distance(piece.point(middle)) + piece.point(middle).distance(piece.point(b));
    if ta.dot(tm) < 0.99875 || tm.dot(tb) < 0.99875 || broken - chord > broken.max(1.0) * 0.0001 {
        if depth >= 16 || result.len() >= 8192 { return None; }
        divide_path(piece, a, middle, depth + 1, result)?;
        divide_path(piece, middle, b, depth + 1, result)?;
    } else { result.push(b); }
    Some(())
}
fn divided(piece: &Piece) -> Option<Vec<f64>> {
    let mut parameters = vec![0.0];
    divide_path(piece, 0.0, 1.0, 0, &mut parameters)?;
    Some(parameters)
}
fn walk(pieces: &[Piece], first: Frame, bank: bool) -> Option<Vec<(Vec<f64>, Vec<Frame>)>> {
    let mut frame = first;
    let mut previous_point = pieces.first()?.point(0.0);
    let mut previous_tangent = pieces.first()?.tangent(0.0)?;
    let mut walks = Vec::new();
    for piece in pieces {
        if !piece.length().is_finite() || piece.length() <= 1e-12 { return None; }
        let tangent = piece.tangent(0.0)?;
        if previous_tangent.dot(tangent) <= -1.0 + 1e-10 { return None; }
        frame = moved(frame, previous_tangent, tangent, previous_point, piece.point(0.0))?;
        let parameters = divided(piece)?;
        let mut frames = vec![frame];
        for span in parameters.windows(2) {
            frame = curved_transport(frame, piece, span[0], span[1], bank)?;
            frames.push(frame);
        }
        previous_point = piece.point(1.0);
        previous_tangent = piece.tangent(1.0)?;
        walks.push((parameters, frames));
    }
    Some(walks)
}

fn emit(value: f64) { println!("{value:.17e}"); }
fn emit_vec(value: Vec3) { for coordinate in [value.x, value.y, value.z] { emit(coordinate); } }
fn emit_frame(value: Frame) { for vector in [value.origin, value.x, value.y] { emit_vec(vector); } }
fn emit_tangent(value: Option<Vec3>) {
    emit(value.is_some() as u8 as f64);
    if let Some(vector) = value { emit_vec(vector); }
}
fn main() {
    let case: usize = std::env::args().nth(1).unwrap().parse().unwrap();
    let arc = || Piece::Planar(Plane::XY, Curve::Arc(Arc {
        centre: [0.0, 0.0], radius: 1.0, start_angle: 0.0,
        end_angle: std::f64::consts::FRAC_PI_2,
    }), case != 5);
    let spline = || {
        if case == 7 || case == 9 {
            NurbsCurve3::new_strict(3,
                vec![[0.0, 0.0, 0.0], [1.0, 1.0, 0.0], [2.0, 0.0, 1.0], [3.0, 1.0, 2.0]],
                vec![0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0], vec![1.0; 4]).unwrap()
        } else {
            NurbsCurve3::new_strict(2,
                vec![[0.0, 0.0, 0.0], [1.0, 1.0, 0.0], [2.0, 0.0, 0.0]],
                vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], vec![1.0; 3]).unwrap()
        }
    };
    let line = Piece::Line(Vec3::ZERO, Vec3::new(0.0, 0.0, 2.0));
    let pieces = match case {
        0 => vec![line.clone()],
        1 => vec![line.clone(), Piece::Line(Vec3::new(0.0, 0.0, 2.0), Vec3::new(2.0, 0.0, 2.0))],
        2 => vec![line.clone(), Piece::Line(Vec3::new(0.0, 0.0, 2.0), Vec3::ZERO)],
        3 | 4 | 5 => vec![arc()],
        6 | 7 | 9 => vec![Piece::Spline(spline(), 0.0, 1.0)],
        8 => vec![Piece::Line(Vec3::ZERO, Vec3::ZERO)],
        _ => panic!("unknown case"),
    };
    let bank = matches!(case, 4 | 7);
    let piece = &pieces[0];
    emit_vec(piece.point(0.5));
    emit_tangent(piece.tangent(0.0));
    emit_tangent(piece.tangent(0.5));
    emit_tangent(piece.tangent(1.0));
    emit(piece.speed(0.5));
    emit(piece.length_to(0.4));
    emit(piece.length());
    let parameters = divided(piece);
    emit(parameters.is_some() as u8 as f64);
    if let Some(parameters) = parameters {
        emit(parameters.len() as f64);
        for parameter in parameters { emit(parameter); }
    }
    let first = Frame { origin: piece.point(0.0), x: Vec3::X, y: Vec3::Y };
    let algebra = first.plus(Frame { origin: Vec3::new(1.0, 2.0, 3.0), x: Vec3::Y, y: Vec3::Z });
    emit_frame(first.lerp(algebra, 0.25));
    emit_vec(first.point([2.0, 3.0]));
    emit(frame_error(first, algebra, 2.0));
    let walked = walk(&pieces, first, bank);
    emit(walked.is_some() as u8 as f64);
    if let Some(walked) = walked {
        emit(walked.len() as f64);
        for (parameters, frames) in walked {
            emit(parameters.len() as f64);
            for parameter in parameters { emit(parameter); }
            emit(frames.len() as f64);
            for frame in frames { emit_frame(frame); }
        }
    }
}
