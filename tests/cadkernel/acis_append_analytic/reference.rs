// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatRecord, SatToken};
use cadkernel::brep::{make, Body, Coedge, Curve3, Edge, Ellipse3, Face, Loop, Lump, Provenance, Shell, Surface, Vertex};
use cadkernel::geom2d::{Arc, Curve, Line};
use cadkernel::space::Plane;
use std::f64::consts::FRAC_PI_2;

// A bounded ellipse sheet with the same insertion order as the native
// planar-region builder. The pinned planar-region maker copies a bottom cap
// through an extrusion and thus assigns different arena slots to equal
// geometry; this direct construction isolates append record ordering.
fn bounded_ellipse_sheet() -> Body {
    let plane = Plane::orthonormal([2.0, -1.0, 0.5], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]).unwrap();
    let ellipse = Curve3::Ellipse(Ellipse3 { plane, major_radius: 3.0, minor_radius: 1.25 });
    let mut body = Body::new();
    let lump = body.lumps.insert(Lump { shells: Vec::new(), provenance: Provenance::Synthesized });
    let shell = body.shells.insert(Shell { faces: Vec::new(), owner: lump, provenance: Provenance::Synthesized });
    let surface = body.surfaces.insert(Surface::Plane(plane));
    let face = body.faces.insert(Face { surface, forward: true, loops: Vec::new(), owner: shell,
        provenance: Provenance::Synthesized });
    let vertices: Vec<_> = (0..4).map(|i| body.vertices.insert(Vertex {
        point: ellipse.point_at(i as f64 * FRAC_PI_2), provenance: Provenance::Synthesized,
    })).collect();
    let ring = body.loops.insert(Loop { coedges: Vec::new(), owner: face,
        provenance: Provenance::Synthesized });
    for i in 0..4 {
        let curve = body.curves.insert(ellipse.clone());
        let edge = body.edges.insert(Edge { curve, start_parameter: i as f64 * FRAC_PI_2,
            end_parameter: (i + 1) as f64 * FRAC_PI_2, start: vertices[i],
            end: vertices[(i + 1) % 4], coedges: Vec::new(), provenance: Provenance::Synthesized });
        let coedge = body.coedges.insert(Coedge { edge, forward: true, pcurve: None, owner: ring,
            provenance: Provenance::Synthesized });
        body.edges.get_mut(edge).unwrap().coedges.push(coedge);
        body.loops.get_mut(ring).unwrap().coedges.push(coedge);
    }
    body.faces.get_mut(face).unwrap().loops.push(ring);
    body.shells.get_mut(shell).unwrap().faces.push(face);
    body.lumps.get_mut(lump).unwrap().shells.push(shell);
    body.roots.push(lump);
    body
}

fn bits(value: f64) -> String {
    let bits = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", (bits >> 32) as u32, bits as u32)
}

fn trace(document: &SatDocument) {
    for record in &document.records {
        let mut line = format!("{};{};{}", record.index, record.entity_type, record.tokens.len());
        for token in &record.tokens {
            match token {
                SatToken::Pointer(value) => line.push_str(&format!(";p{}", value.0)),
                SatToken::Position(x, y, z) => {
                    line.push_str(&format!(";v{},{},{}", bits(*x), bits(*y), bits(*z)))
                }
                SatToken::Float(value) => line.push_str(&format!(";f{}", bits(*value))),
                SatToken::Integer(value) => line.push_str(&format!(";n{value}")),
                SatToken::Ident(value) => line.push_str(&format!(";i{value}")),
                SatToken::Enum(value) => line.push_str(&format!(";e{value}")),
                other => panic!("unexpected SAT token: {other:?}"),
            }
        }
        println!("{line}");
    }
}

fn source(case: usize) -> Body {
    let mut body = match case {
        0 | 4 | 5 => make::cylinder([1.0, 2.0, 0.5], 2.0, 3.0),
        1 => make::cone([-2.0, 1.0, 0.5], 2.5, 4.0),
        2 => make::torus([1.0, -1.0, 2.0], 5.0, 1.5),
        3 => Some(bounded_ellipse_sheet()),
        _ => panic!("unknown case"),
    }.expect("bounded source");
    if case == 4 || case == 5 {
        let key = body.coedges.keys().next().unwrap();
        body.coedges.get_mut(key).unwrap().pcurve = Some(if case == 4 {
            Curve::Line(Line { start: [0.0, 0.0], end: [1.0, 0.0] })
        } else {
            Curve::Arc(Arc { centre: [0.0, 0.0], radius: 1.0,
                start_angle: 0.0, end_angle: 1.0 })
        });
    }
    assert!(body.validate().is_empty(), "source topology");
    body
}

fn main() {
    let case: usize = std::env::args().nth(1).expect("case").parse().expect("case number");
    let body = source(case);
    let mut document = SatDocument::new();
    assert_eq!(document.add_record(SatRecord::new(-1, "seed")), 0);
    let written = cadkernel::acis::append(&body, &mut document).unwrap();
    let pcurves = body.coedges.iter().filter(|(_, use_node)| use_node.pcurve.is_some()).count();
    println!("summary;{case};{};{};{};{pcurves}", written.body, written.records,
        document.record_count());
    trace(&document);
}
