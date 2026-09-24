// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{
    acis::{self, Unwritable},
    brep::{make, Body, Curve3, Line3, Provenance, SourceRef, Surface},
    space::Plane,
};

fn pointer(id: i32) -> SatToken {
    SatToken::Pointer(SatPointer::new(id))
}

fn record(document: &mut SatDocument, kind: &str, tokens: Vec<SatToken>) {
    let mut entry = SatRecord::new(-1, kind);
    entry.tokens = tokens;
    document.add_record(entry);
}

fn document() -> SatDocument {
    let mut result = SatDocument::new();
    record(&mut result, "point", vec![pointer(-1), SatToken::Ident("old-point".into())]);
    record(&mut result, "edge", vec![pointer(-1), pointer(-1), SatToken::Float(0.0), pointer(-1), SatToken::Float(1.0), pointer(-1), pointer(6), SatToken::Ident("forward".into())]);
    record(&mut result, "plane-surface", vec![pointer(-1), SatToken::Ident("old-plane".into())]);
    record(&mut result, "marker", vec![SatToken::Ident("untouched".into())]);
    record(&mut result, "vertex", vec![pointer(-1), pointer(1), SatToken::Integer(7), pointer(0)]);
    record(&mut result, "face", vec![pointer(-1), pointer(-1), pointer(-1), pointer(-1), pointer(-1), pointer(2), SatToken::Ident("forward".into()), SatToken::Ident("single".into())]);
    record(&mut result, "straight-curve", vec![pointer(-1), SatToken::Ident("old-line".into())]);
    let point = result.record_mut(0).unwrap();
    point.attribute = SatPointer::new(3);
    point.subtype_id = 12;
    point.raw_text = Some("raw point".into());
    result.records.swap(0, 5);
    result.records.swap(1, 6);
    result.records.swap(2, 4);
    assert_eq!(result.records.iter().map(|r| r.index).collect::<Vec<_>>(), [5, 6, 4, 3, 2, 0, 1]);
    result
}

fn clean(body: &mut Body) {
    for key in body.faces.keys().collect::<Vec<_>>() {
        body.faces.get_mut(key).unwrap().provenance = Provenance::Clean(SourceRef::new(99));
    }
    for key in body.edges.keys().collect::<Vec<_>>() {
        body.edges.get_mut(key).unwrap().provenance = Provenance::Clean(SourceRef::new(99));
    }
    for key in body.vertices.keys().collect::<Vec<_>>() {
        body.vertices.get_mut(key).unwrap().provenance = Provenance::Clean(SourceRef::new(99));
    }
}

fn dirty_three(body: &mut Body) {
    let face_key = body.faces.keys().next().unwrap();
    let edge_key = body.edges.keys().next().unwrap();
    let vertex_key = body.vertices.keys().next().unwrap();
    let surface_key = body.faces.get(face_key).unwrap().surface;
    let curve_key = body.edges.get(edge_key).unwrap().curve;
    body.faces.get_mut(face_key).unwrap().provenance = Provenance::Dirty(SourceRef::new(5));
    body.edges.get_mut(edge_key).unwrap().provenance = Provenance::Dirty(SourceRef::new(1));
    let vertex = body.vertices.get_mut(vertex_key).unwrap();
    vertex.provenance = Provenance::Dirty(SourceRef::new(4));
    vertex.point = [9.0, 8.0, 7.0];
    *body.surfaces.get_mut(surface_key).unwrap() = Surface::Plane(Plane::from_axes(
        [1.0, 2.0, 3.0], [0.0, 1.0, 0.0], [-1.0, 0.0, 0.0],
    ));
    *body.curves.get_mut(curve_key).unwrap() = Curve3::Line(Line3 {
        origin: [2.0, 3.0, 4.0], direction: [5.0, 0.0, 0.0],
    });
}

fn bits(value: f64) -> String {
    let raw = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", (raw >> 32) as u32, raw as u32)
}

fn trace(document: &SatDocument) {
    for (slot, record) in document.records.iter().enumerate() {
        let mut line = format!(
            "{};{};{};{};{};{};{}", slot, record.index, record.entity_type,
            record.attribute.0, record.subtype_id,
            record.raw_text.as_deref().unwrap_or("-"), record.tokens.len(),
        );
        for token in &record.tokens {
            match token {
                SatToken::Pointer(value) => line.push_str(&format!(";p{}", value.0)),
                SatToken::Position(x, y, z) => line.push_str(&format!(";v{},{},{}", bits(*x), bits(*y), bits(*z))),
                SatToken::Float(value) => line.push_str(&format!(";f{}", bits(*value))),
                SatToken::Integer(value) => line.push_str(&format!(";n{value}")),
                SatToken::Ident(value) => line.push_str(&format!(";i{}", value)),
                other => panic!("unexpected token: {other:?}"),
            }
        }
        println!("{line}");
    }
}

fn main() {
    let mut body = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    assert_eq!(acis::pending(&body), 26);
    println!("pending_synth;26");
    clean(&mut body);
    assert_eq!(acis::pending(&body), 0);
    let mut document = document();
    let before = document.clone();
    assert_eq!(acis::lower(&body, &mut document), Ok(0));
    assert_eq!(document, before);
    println!("pending_clean;0");
    dirty_three(&mut body);
    assert_eq!(acis::pending(&body), 3);
    println!("pending_dirty;3");
    assert_eq!(acis::lower(&body, &mut document), Ok(3));
    assert_eq!(document.record(0).unwrap().attribute, SatPointer::new(3));
    assert_eq!(document.record(0).unwrap().subtype_id, 12);
    assert_eq!(document.record(0).unwrap().raw_text.as_deref(), Some("raw point"));
    println!("lower_written;3");
    trace(&document);

    let first_face = body.faces.keys().next().unwrap();
    let first_edge = body.edges.keys().next().unwrap();
    let first_vertex = body.vertices.keys().next().unwrap();
    body.faces.get_mut(first_face).unwrap().provenance = Provenance::Clean(SourceRef::new(5));
    body.edges.get_mut(first_edge).unwrap().provenance = Provenance::Clean(SourceRef::new(1));
    assert_eq!(acis::pending(&body), 1);
    println!("pending_vertex;1");

    let mut wrong = before.clone();
    wrong.record_mut(0).unwrap().entity_type = "plane-surface".into();
    let snapshot = wrong.clone();
    match acis::lower(&body, &mut wrong) {
        Err(Unwritable::WrongRecord { found, expected }) => {
            println!("wrong;{found};{expected}");
            assert_eq!((found.as_str(), expected.as_str()), ("plane-surface", "point"));
        }
        other => panic!("expected wrong-record refusal: {other:?}"),
    }
    assert_eq!(wrong, snapshot);

    body.vertices.get_mut(first_vertex).unwrap().provenance = Provenance::Dirty(SourceRef::new(50));
    let mut missing = before.clone();
    assert_eq!(acis::lower(&body, &mut missing), Err(Unwritable::MissingSource(50)));
    assert_eq!(missing, before);
    println!("missing_source;50");

    body.vertices.get_mut(first_vertex).unwrap().provenance = Provenance::Dirty(SourceRef::new(4));
    let mut missing_target = before.clone();
    missing_target.record_mut(4).unwrap().tokens[3] = pointer(99);
    let snapshot = missing_target.clone();
    assert_eq!(acis::lower(&body, &mut missing_target), Err(Unwritable::MissingSource(4)));
    assert_eq!(missing_target, snapshot);
    println!("missing_target;4");
}
