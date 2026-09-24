// SPDX-License-Identifier: MPL-2.0
// Public path sweep from pinned src/brep/sweep_path.rs at 953d546b68aef4b6692566a1a9b077fc5bd9fb4f.
use cadkernel::brep::{sweep_path, Provenance, Surface, SweepOptions, SweepPath};
use cadkernel::geom2d::{Curve, Line, NurbsCurve};
use cadkernel::space::{NurbsCurve3, Plane};

fn emit(body: &cadkernel::brep::Body) {
    let face_key = body.face_keys().next().unwrap();
    let face = body.faces.get(face_key).unwrap();
    let surface = body.surfaces.get(face.surface).unwrap();
    let point = surface.point_at(0.25, 0.75);
    let second = surface.point_at(0.75, 0.25);
    let shared = body.edge_keys().filter(|key| body.edges.get(*key).unwrap().coedges.len() == 2).count();
    let pcurves = body.coedges.keys().filter(|key| body.coedges.get(*key).unwrap().pcurve.is_some()).count();
    let synthesized = body.vertices.keys().all(|key| matches!(body.vertices.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.edges.keys().all(|key| matches!(body.edges.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.coedges.keys().all(|key| matches!(body.coedges.get(key).unwrap().provenance, Provenance::Synthesized))
        && body.faces.keys().all(|key| matches!(body.faces.get(key).unwrap().provenance, Provenance::Synthesized))
        && matches!(body.provenance, Provenance::Synthesized);
    assert!(matches!(surface, Surface::Nurbs(_)));
    println!("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {:.16} {:.16} {:.16} {:.16} {:.16} {:.16}",
        body.vertices.len(), body.edges.len(), body.coedges.len(), body.loops.len(), body.faces.len(),
        body.shells.len(), body.lumps.len(), body.surfaces.len(), body.curves.len(), body.roots.len(),
        body.validate().len(), shared, pcurves, u8::from(face.forward), u8::from(synthesized), 5,
        point[0], point[1], point[2], second[0], second[1], second[2]);
}

fn main() {
    for (two, reverse, rational) in [(false, false, false), (true, false, false),
        (false, true, false), (false, false, true)] {
        let controls = if two { vec![[0.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 0.0, 2.0],
            [0.0, 0.0, 3.0], [0.0, 0.0, 4.0]] }
            else { vec![[0.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 0.0, 2.0]] };
        let knots = if two { vec![0.0, 0.0, 0.0, 0.5, 0.5, 1.0, 1.0, 1.0] }
            else { vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0] };
        let path = NurbsCurve3::new_strict(2, controls.clone(), knots, vec![1.0; controls.len()]).unwrap();
        let plane = if reverse { Plane::from_axes([0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, -1.0, 0.0]) }
            else { Plane::XY };
        let profile_curve = if rational {
            Curve::Nurbs(NurbsCurve::new_strict(2, vec![[0.0, 0.0], [0.5, 1.0], [1.0, 0.0]],
                vec![0.0, 0.0, 0.0, 1.0, 1.0, 1.0], vec![1.0, 2.0, 1.0]).unwrap())
        } else { Curve::Line(Line { start: [0.0, 0.0], end: [1.0, 0.0] }) };
        let profile = vec![vec![profile_curve]];
        let options = SweepOptions { align: !reverse, base_point: Some([0.0, 0.0, 0.0]), surface: true, ..Default::default() };
        let body = sweep_path(plane, &profile, SweepPath::Nurbs3(&path), options).unwrap();
        emit(&body);
    }
}
