// SPDX-License-Identifier: MPL-2.0
// Public pinned B-rep loft reference for open profile sections.
use cadkernel::brep::{loft_with_options, LoftOptions, LoftSection};
use cadkernel::geom2d::{Arc, Curve, Line, Ray};
use cadkernel::space::Plane;

fn next(args: &[String], at: &mut usize) -> f64 {
    let value = args[*at].parse::<f64>().unwrap();
    *at += 1;
    value
}
fn point2(args: &[String], at: &mut usize) -> [f64; 2] {
    [next(args, at), next(args, at)]
}
fn point3(args: &[String], at: &mut usize) -> [f64; 3] {
    [next(args, at), next(args, at), next(args, at)]
}
fn emit(value: f64) { println!("{value:.17e}"); }
fn point(value: [f64; 3]) { for component in value { emit(component); } }
fn main() {
    let args = std::env::args().skip(1).collect::<Vec<_>>();
    let mut at = 0;
    let cyclic = next(&args, &mut at) != 0.0;
    let periodic = next(&args, &mut at) != 0.0;
    let matching = next(&args, &mut at) != 0.0;
    let mode = next(&args, &mut at) as i32;
    let start_angle = next(&args, &mut at);
    let end_angle = next(&args, &mut at);
    let start_magnitude = next(&args, &mut at);
    let end_magnitude = next(&args, &mut at);
    let count = next(&args, &mut at) as usize;
    let mut sections = Vec::new();
    for _ in 0..count {
        let plane = Plane::from_axes(point3(&args, &mut at), point3(&args, &mut at), point3(&args, &mut at));
        let closed = next(&args, &mut at) != 0.0;
        let wire_count = next(&args, &mut at) as usize;
        let mut wires = Vec::new();
        for _ in 0..wire_count {
            let piece_count = next(&args, &mut at) as usize;
            let mut pieces = Vec::new();
            for _ in 0..piece_count {
                let kind = next(&args, &mut at) as i32;
                let curve = match kind {
                    0 => Curve::Line(Line { start: point2(&args, &mut at), end: point2(&args, &mut at) }),
                    2 => Curve::Arc(Arc { centre: point2(&args, &mut at), radius: next(&args, &mut at),
                        start_angle: next(&args, &mut at), end_angle: next(&args, &mut at) }),
                    5 => Curve::Ray(Ray { origin: point2(&args, &mut at), direction: point2(&args, &mut at) }),
                    _ => panic!("unknown curve kind {kind}"),
                };
                pieces.push(curve);
            }
            wires.push(pieces);
        }
        sections.push(LoftSection::Profile { plane, wires, closed });
    }
    assert_eq!(at, args.len());
    let mut options = LoftOptions::default();
    options.closed = cyclic;
    options.periodic = periodic;
    options.align_direction = matching;
    options.normals = mode;
    options.start_draft_angle = start_angle;
    options.end_draft_angle = end_angle;
    options.start_magnitude = start_magnitude;
    options.end_magnitude = end_magnitude;
    let Ok(body) = loft_with_options(&sections, &[], None, options) else { emit(0.0); return; };
    emit(1.0);
    for count in [body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(),
        body.faces.len(), body.shells.len(), body.lumps.len(), body.surfaces.len(), body.curves.len(),
        body.roots.len(), body.validate().len()] { emit(count as f64); }
    for root in &body.roots { emit(root.slot() as f64); }
    for (_, vertex) in body.vertices.iter() { point(vertex.point); }
    for (_, edge) in body.edges.iter() {
        emit(edge.start.slot() as f64); emit(edge.end.slot() as f64);
        emit(edge.coedges.len() as f64);
        for usage in &edge.coedges { emit(usage.slot() as f64); }
        let curve = body.curves.get(edge.curve).unwrap();
        for t in [0.25, 0.5, 0.75] { point(curve.point_at(t)); }
    }
    for (_, usage) in body.coedges.iter() {
        emit(usage.edge.slot() as f64); emit(u8::from(usage.forward) as f64);
        emit(usage.owner.slot() as f64);
        let Some(Curve::Line(line)) = &usage.pcurve else { panic!("expected linear pcurve") };
        for value in [line.start[0], line.start[1], line.end[0], line.end[1]] { emit(value); }
    }
    for (_, ring) in body.loops.iter() {
        emit(ring.owner.slot() as f64); emit(ring.coedges.len() as f64);
        for usage in &ring.coedges { emit(usage.slot() as f64); }
    }
    for (_, face) in body.faces.iter() {
        emit(u8::from(face.forward) as f64); emit(face.owner.slot() as f64);
        emit(face.surface.slot() as f64); emit(face.loops.len() as f64);
        for ring in &face.loops { emit(ring.slot() as f64); }
        let surface = body.surfaces.get(face.surface).unwrap();
        point(surface.point_at(0.25, 0.5));
        point(surface.point_at(0.5, 0.75));
    }
    for (_, shell) in body.shells.iter() {
        emit(shell.owner.slot() as f64); emit(shell.faces.len() as f64);
        for face in &shell.faces { emit(face.slot() as f64); }
    }
    for (_, lump) in body.lumps.iter() {
        emit(lump.shells.len() as f64);
        for shell in &lump.shells { emit(shell.slot() as f64); }
    }
}
