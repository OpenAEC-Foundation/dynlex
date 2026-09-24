// SPDX-License-Identifier: MPL-2.0
// Public Rust sweep_path from pinned src/brep/sweep_path.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{sweep_path, Body, Provenance, Surface, SweepOptions, SweepPath};
use cadkernel::geom2d::{Curve, Line, NurbsCurve};
use cadkernel::space::{NurbsCurve3, Plane};

fn emit(body: &Body) {
    let face_key = body.face_keys().next().unwrap();
    let face = body.faces.get(face_key).unwrap();
    let surface = body.surfaces.get(face.surface).unwrap();
    let surface_kind = match surface { Surface::Plane(_) => 0.0, Surface::Nurbs(_) => 5.0, _ => panic!("unexpected surface") };
    let first_sample = surface.point_at(0.25, 0.75);
    let second_sample = surface.point_at(0.75, 0.25);
    let shared = body.edge_keys().filter(|key| body.edges.get(*key).unwrap().coedges.len() == 2).count();
    let pcurves = body.coedges.keys().filter(|key| body.coedges.get(*key).unwrap().pcurve.is_some()).count();
    let synthesized = matches!(body.provenance, Provenance::Synthesized)
        && body.vertices.keys().all(|key| matches!(body.vertices.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.edges.keys().all(|key| matches!(body.edges.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.coedges.keys().all(|key| matches!(body.coedges.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.loops.keys().all(|key| matches!(body.loops.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.faces.keys().all(|key| matches!(body.faces.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.shells.keys().all(|key| matches!(body.shells.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.lumps.keys().all(|key| matches!(body.lumps.get(key).unwrap().provenance, Provenance::Synthesized));
    let first_vertex = body.vertices.get(body.vertices.keys().next().unwrap()).unwrap().point;
    let last_vertex = body.vertices.get(body.vertices.keys().last().unwrap()).unwrap().point;
    let mut values = vec![
        body.vertices.len() as f64, body.edges.len() as f64, body.coedges.len() as f64,
        body.loops.len() as f64, body.faces.len() as f64, body.shells.len() as f64,
        body.lumps.len() as f64, body.surfaces.len() as f64, body.curves.len() as f64,
        body.roots.len() as f64, body.validate().len() as f64, shared as f64,
        pcurves as f64, u8::from(face.forward) as f64, u8::from(synthesized) as f64, surface_kind,
    ];
    values.extend(first_sample);
    values.extend(second_sample);
    values.extend(first_vertex);
    values.extend(last_vertex);
    for value in values { println!("{value:.17}"); }
}

fn main() {
    for case in 0..11 {
        let shift = if case == 2 { [2.0, 1.0] } else { [0.0, 0.0] };
        let path_controls = if case == 6 { (0..5).map(|step| [shift[0], shift[1], step as f64]).collect::<Vec<_>>() }
            else { vec![[shift[0], shift[1], 0.0], [shift[0], shift[1], 1.0], [shift[0], shift[1], 2.0]] };
        let path_knots = if case == 6 { vec![0.0, 0.0, 0.0, 0.5, 0.5, 1.0, 1.0, 1.0] }
            else { vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0] };
        let mut path_weights = vec![1.0; path_controls.len()];
        if case == 7 { path_weights[1] = 2.0; }
        let path = NurbsCurve3::new_strict(2, path_controls.clone(), path_knots, path_weights).unwrap();
        let profile = if case == 2 {
            Curve::Nurbs(NurbsCurve::new_strict(2, vec![[0.0, 0.0], [0.5, 1.0], [1.0, 0.0]],
                vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], vec![1.0, 2.0, 1.0]).unwrap())
        } else { Curve::Line(Line { start: [0.0, 0.0], end: [1.0, 0.0] }) };
        let plane = if case == 3 {
            Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0])
        } else { Plane::XY };
        let options = SweepOptions {
            align: case != 3,
            base_point: if case == 1 { None }
                else if case == 9 || case == 10 { Some([0.0, 0.0, 1.0]) }
                else { Some([0.0, 0.0, 0.0]) },
            rotation: if case == 4 { 0.4 } else { 0.0 },
            surface: case == 0,
            ..Default::default()
        };
        let line_points = if case == 8 { [[0.0, 0.0, 2.0], [0.0, 0.0, 0.0]] }
            else { [[0.0, 0.0, 0.0], [0.0, 0.0, 2.0]] };
        let route = if case == 5 || case == 8 || case == 10 { SweepPath::Polyline3d { points: &line_points, closed: false } }
            else { SweepPath::Nurbs3(&path) };
        let body = sweep_path(plane, &[vec![profile]], route, options).unwrap();
        emit(&body);
    }
}
