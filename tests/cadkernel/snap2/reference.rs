// SPDX-License-Identifier: MPL-2.0
// Decode test inputs only. All geometry is evaluated by pinned source modules.
use geom2d::{curve::*, polyline::*, nurbs::NurbsCurve, Ellipse, Tolerance};
use std::sync::atomic::{AtomicU64, Ordering};
static SCALE: AtomicU64 = AtomicU64::new(0);
fn record_scale(values: impl IntoIterator<Item = f64>) {
    for value in values {
        if value.is_finite() {
            let previous = f64::from_bits(SCALE.load(Ordering::Relaxed));
            SCALE.store(previous.max(value.abs()).to_bits(), Ordering::Relaxed);
        }
    }
}

pub(crate) fn shape(a: &[f64]) -> Curve {
    let n = a[7] as usize;
    let nk = a[10] as usize;
    let nw = a[11] as usize;
    let start = [a[12], a[13]];
    let end = [a[14], a[15]];
    let vertices: Vec<_> = (0..n).map(|i| PolylineVertex::curved(
        [a[22 + 3*i], a[23 + 3*i]], a[24 + 3*i])).collect();
    let kind = a[0] as usize;
    if kind != 4 && kind != 5 { record_scale(start); }
    if kind == 0 || kind == 6 || kind == 7 { record_scale(end); }
    if (1..=3).contains(&kind) { record_scale([a[16]]); }
    if kind == 3 { record_scale([a[16]*a[20], a[16]*a[21], a[19]*a[20], a[19]*a[21]]); }
    if kind == 4 || kind == 5 {
        for vertex in &vertices { record_scale(vertex.position); }
    }
    match a[0] as usize {
        0 => Curve::Line(Line { start, end }),
        1 => Curve::Circle(Circle { centre: start, radius: a[16] }),
        2 => Curve::Arc(Arc { centre: start, radius: a[16], start_angle: a[17], end_angle: a[18] }),
        3 => Curve::Ellipse(EllipseArc { ellipse: Ellipse {
            centre: start, major_radius: a[16], minor_radius: a[19], major_axis: [a[20], a[21]] },
            start_parameter: a[17], end_parameter: a[18] }),
        4 => Curve::Polyline(Polyline { vertices, closed: a[8] != 0. }),
        5 => {
            let curve = NurbsCurve::new(a[9] as usize, vertices.iter().map(|v| v.position).collect(),
                a[22+3*n..22+3*n+nk].to_vec(), Some(a[22+3*n+nk..22+3*n+nk+nw].to_vec()));
            integer(usize::from(curve.is_some()));
            Curve::Nurbs(curve.expect("valid test NURBS"))
        },
        6 => Curve::Ray(Ray { origin: start, direction: end }),
        7 => Curve::XLine(XLine { base: start, direction: end }),
        _ => unreachable!(),
    }
}
pub(crate) fn number(x: f64) { println!("{x:.17e}"); }
pub(crate) fn integer(x: usize) { println!("d {x}"); }
pub(crate) fn point(p: [f64; 2]) {
    println!("p {:.17e} {:.17e} {:.17e}", f64::from_bits(SCALE.load(Ordering::Relaxed)), p[0], p[1]);
}

fn snap(p:geom2d::snap::SnapPoint) {
    use geom2d::snap::SnapKind::*;
    integer(match p.kind{Endpoint=>0,Midpoint=>1,Centre=>2,Quadrant=>3,Perpendicular=>4,Tangent=>5});
    point(p.point);number(p.t);integer(usize::from(p==p.clone()));
}
fn main() {
    use geom2d::snap::*;
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let target=[a[1],a[2]];
    let curve=shape(&a[4..]);
    let before=curve.clone();
    if a[0]==3. {
        snap(nearest_to(&curve,target));
    } else {
        let points=match a[0] as usize {0=>characteristic_points(&curve),1=>perpendicular_from(&curve,target),2=>tangent_from(&curve,target),_=>panic!("mode")};
        integer(points.len());for point in points{snap(point);}
    }
    integer(usize::from(curve==before));
}
