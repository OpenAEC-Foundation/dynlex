// SPDX-License-Identifier: MPL-2.0
// Isolated private formulas from pinned src/brep/sweep_path.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::space::Vec3;

#[derive(Clone, Copy)]
struct Frame { origin: Vec3, x: Vec3, y: Vec3 }

impl Frame {
    fn point(self, p: [f64; 2]) -> Vec3 { self.origin + self.x * p[0] + self.y * p[1] }
    fn plus(self, other: Self) -> Self {
        Self { origin: self.origin + other.origin, x: self.x + other.x, y: self.y + other.y }
    }
    fn minus(self, other: Self) -> Self { self.plus(other.times(-1.0)) }
    fn times(self, t: f64) -> Self {
        Self { origin: self.origin * t, x: self.x * t, y: self.y * t }
    }
}

type Patch = [Frame; 4];

fn bezier(p: &Patch, t: f64) -> Frame {
    let s = 1.0 - t;
    p[0].times(s * s * s).plus(p[1].times(3.0 * s * s * t))
        .plus(p[2].times(3.0 * s * t * t)).plus(p[3].times(t * t * t))
}

fn bezier_derivative(p: &Patch, t: f64) -> Frame {
    let s = 1.0 - t;
    p[1].minus(p[0]).times(3.0 * s * s)
        .plus(p[2].minus(p[1]).times(6.0 * s * t))
        .plus(p[3].minus(p[2]).times(3.0 * t * t))
}

fn frame_error(a: Frame, b: Frame, radius: f64) -> f64 {
    a.origin.distance(b.origin) + radius * (a.x.distance(b.x) + a.y.distance(b.y))
}

fn fit_patch(
    evaluate: &impl Fn(f64) -> Option<Frame>, a: f64, b: f64,
    radius: f64, tolerance: f64, depth: usize, result: &mut Vec<Patch>,
) -> Option<()> {
    let p0 = evaluate(a)?;
    let p3 = evaluate(b)?;
    let q1 = evaluate(a + (b - a) / 3.0)?;
    let q2 = evaluate(a + (b - a) * 2.0 / 3.0)?;
    let c = q1.times(27.0).minus(p0.times(8.0)).minus(p3);
    let d = q2.times(27.0).minus(p0).minus(p3.times(8.0));
    let patch = [p0, c.times(2.0).minus(d).times(1.0 / 18.0),
        d.times(2.0).minus(c).times(1.0 / 18.0), p3];
    let mut error = 0.0_f64;
    for t in [0.125, 0.25, 0.5, 0.75, 0.875] {
        error = error.max(frame_error(bezier(&patch, t), evaluate(a + (b - a) * t)?, radius));
    }
    if !error.is_finite() { return None; }
    if error > tolerance {
        if depth >= 14 || result.len() >= 8192 { return None; }
        let middle = (a + b) * 0.5;
        fit_patch(evaluate, a, middle, radius, tolerance, depth + 1, result)?;
        fit_patch(evaluate, middle, b, radius, tolerance, depth + 1, result)?;
    } else {
        result.push(patch);
    }
    Some(())
}

fn regular_transport(points: &[[f64; 2]], patches: &[Patch]) -> Option<bool> {
    let mut orientation = None;
    for patch in patches {
        for at in [0.0, 0.125, 0.25, 0.5, 0.75, 0.875, 1.0] {
            let frame = bezier(patch, at);
            let derivative = bezier_derivative(patch, at);
            let normal = frame.x.cross(frame.y);
            let normal_length = normal.length();
            if !normal_length.is_finite() || normal_length <= 0.0 { return None; }
            for point in points {
                let velocity = derivative.point(*point);
                let jacobian = normal.dot(velocity);
                let tolerance = normal_length * velocity.length() * 1e-10;
                if !jacobian.is_finite() || !tolerance.is_finite() || jacobian.abs() <= tolerance {
                    return None;
                }
                let forward = jacobian > 0.0;
                if orientation.is_some_and(|previous| previous != forward) { return None; }
                orientation = Some(forward);
            }
        }
    }
    orientation
}

fn polynomial(case: usize, t: f64) -> Option<Frame> {
    if case == 2 && t > 0.5 { return None; }
    let z = if case == 1 || case == 4 { t * t * t * t } else { t };
    Some(Frame { origin: Vec3::new(0.0, 0.0, z), x: Vec3::X, y: Vec3::Y })
}

fn spatial(t: f64) -> Option<Frame> {
    Some(Frame {
        origin: Vec3::new(t, 0.5 * t * t * t, t * t),
        x: Vec3::new(1.0, 0.2 * t, 0.0),
        y: Vec3::new(0.0, 1.0, 0.1 * t * t),
    })
}

fn emit(value: f64) { println!("{value:.17e}"); }
fn emit_vec(value: Vec3) { for coordinate in [value.x, value.y, value.z] { emit(coordinate); } }
fn emit_frame(value: Frame) { for vector in [value.origin, value.x, value.y] { emit_vec(vector); } }

fn main() {
    let case: usize = std::env::args().nth(1).unwrap().parse().unwrap();
    let tolerance = if case == 1 { 1e-5 } else if case == 4 { 1e-18 } else { 1e-7 };
    let mut patches = Vec::new();
    let fitted = if case == 3 {
        fit_patch(&spatial, 0.0, 1.0, 1.3, tolerance, 0, &mut patches)
    } else {
        fit_patch(&|t| polynomial(case, t), 0.0, 1.0, 1.3, tolerance, 0, &mut patches)
    };
    if fitted.is_none() { patches.clear(); }
    emit(fitted.is_some() as u8 as f64);
    emit(patches.len() as f64);
    for patch in &patches {
        for frame in patch { emit_frame(*frame); }
        emit_frame(bezier(patch, 0.37));
        emit_frame(bezier_derivative(patch, 0.37));
        emit(frame_error(patch[0], patch[3], 1.3));
    }

    let first = Frame { origin: Vec3::ZERO, x: Vec3::X, y: Vec3::Y };
    let second = Frame { origin: Vec3::new(0.0, 0.0, 1.0), ..first };
    let third = second;
    let last = Frame { origin: Vec3::new(0.0, 0.0, 2.0), ..first };
    let regular_patch = match case {
        1 => [last, third, second, first],
        2 => [first, second, third, first],
        5 => {
            let narrow = Frame { origin: Vec3::ZERO, x: Vec3::X, y: Vec3::X };
            [narrow; 4]
        }
        _ => [first, second, third, last],
    };
    let mut regular_patches = vec![regular_patch];
    if case == 6 { regular_patches.push([last, third, second, first]); }
    let points = if case == 4 { vec![] } else { vec![[0.0, 0.0], [0.4, 0.2]] };
    let regular = regular_transport(&points, &regular_patches);
    emit(regular.is_some() as u8 as f64);
    emit(regular.unwrap_or(false) as u8 as f64);
}
