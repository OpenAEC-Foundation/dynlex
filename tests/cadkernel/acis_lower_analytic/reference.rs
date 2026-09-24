// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{
    acis::{self, Unwritable},
    brep::{make, Body, Circle3, Cone, Curve3, Cylinder, Ellipse3, Provenance, SourceRef, Sphere, Surface, Torus},
    space::Plane,
};

fn pointer(id: i32) -> SatToken { SatToken::Pointer(SatPointer::new(id)) }

fn add(document: &mut SatDocument, kind: &str, tokens: Vec<SatToken>) {
    let mut record = SatRecord::new(-1, kind);
    record.tokens = tokens;
    document.add_record(record);
}

fn document(surface_kind: &str, curve_kind: &str) -> SatDocument {
    let mut doc = SatDocument::new();
    add(&mut doc, "marker", vec![SatToken::Ident("unrelated".into())]);
    add(&mut doc, "face", vec![pointer(-1), pointer(-1), pointer(-1), pointer(-1), pointer(-1), pointer(2), SatToken::Ident("forward".into()), SatToken::Ident("single".into())]);
    add(&mut doc, surface_kind, vec![pointer(-1), SatToken::Ident("old-surface".into())]);
    add(&mut doc, "edge", vec![pointer(-1), pointer(-1), SatToken::Float(0.0), pointer(-1), SatToken::Float(1.0), pointer(-1), pointer(4), SatToken::Ident("forward".into())]);
    add(&mut doc, curve_kind, vec![pointer(-1), SatToken::Ident("old-curve".into())]);
    add(&mut doc, surface_kind, vec![pointer(-1), SatToken::Ident("other-surface".into())]);
    add(&mut doc, curve_kind, vec![pointer(-1), SatToken::Ident("other-curve".into())]);
    for id in [2, 4] {
        let target = doc.record_mut(id).unwrap();
        target.attribute = SatPointer::new(0);
        target.subtype_id = 17 + id as i32;
        target.raw_text = Some(format!("raw-{id}"));
    }
    doc.records.swap(0, 4);
    doc.records.swap(1, 6);
    doc.records.swap(2, 3);
    assert_eq!(doc.records.iter().map(|r| r.index).collect::<Vec<_>>(), [4, 6, 3, 2, 0, 5, 1]);
    doc
}

fn clean(body: &mut Body) {
    let mark = Provenance::Clean(SourceRef::new(99));
    for key in body.faces.keys().collect::<Vec<_>>() { body.faces.get_mut(key).unwrap().provenance = mark; }
    for key in body.edges.keys().collect::<Vec<_>>() { body.edges.get_mut(key).unwrap().provenance = mark; }
    for key in body.vertices.keys().collect::<Vec<_>>() { body.vertices.get_mut(key).unwrap().provenance = mark; }
}

fn surface_body(surface: Surface) -> Body {
    let mut body = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    clean(&mut body);
    let face = body.faces.keys().next().unwrap();
    let support = body.faces.get(face).unwrap().surface;
    body.faces.get_mut(face).unwrap().provenance = Provenance::Dirty(SourceRef::new(1));
    *body.surfaces.get_mut(support).unwrap() = surface;
    body
}

fn curve_body(curve: Curve3) -> Body {
    let mut body = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    clean(&mut body);
    let edge = body.edges.keys().next().unwrap();
    let support = body.edges.get(edge).unwrap().curve;
    body.edges.get_mut(edge).unwrap().provenance = Provenance::Dirty(SourceRef::new(3));
    *body.curves.get_mut(support).unwrap() = curve;
    body
}

fn bits(value: f64) -> String {
    let word = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", word >> 32, word as u32)
}

fn trace(label: &str, document: &SatDocument) {
    println!("case;{label}");
    for (slot, record) in document.records.iter().enumerate() {
        let mut line = format!("{};{};{};{};{};{};{}", slot, record.index, record.entity_type,
            record.attribute.0, record.subtype_id,
            record.raw_text.as_deref().unwrap_or("-"), record.tokens.len());
        for token in &record.tokens {
            match token {
                SatToken::Pointer(value) => line.push_str(&format!(";p{}", value.0)),
                SatToken::Position(x, y, z) => line.push_str(&format!(";v{},{},{}", bits(*x), bits(*y), bits(*z))),
                SatToken::Float(value) => line.push_str(&format!(";f{}", bits(*value))),
                SatToken::Integer(value) => line.push_str(&format!(";n{value}")),
                SatToken::Ident(value) => line.push_str(&format!(";i{value}")),
                other => panic!("unexpected token: {other:?}"),
            }
        }
        println!("{line}");
    }
}

fn lower_surface(label: &str, surface: Surface, kind: &str) {
    let body = surface_body(surface);
    let mut doc = document(kind, "straight-curve");
    let before = doc.clone();
    assert_eq!(acis::pending(&body), 1);
    assert_eq!(acis::lower(&body, &mut doc), Ok(1));
    assert_eq!(doc.record(1), before.record(1));
    assert_eq!(doc.record(3), before.record(3));
    assert_eq!(doc.record(5), before.record(5));
    assert_eq!(doc.record(6), before.record(6));
    assert_eq!(doc.record(2).unwrap().attribute, SatPointer::new(0));
    assert_eq!(doc.record(2).unwrap().subtype_id, 19);
    assert_eq!(doc.record(2).unwrap().raw_text.as_deref(), Some("raw-2"));
    trace(label, &doc);
}

fn lower_curve(label: &str, curve: Curve3, kind: &str) {
    let body = curve_body(curve);
    let mut doc = document("plane-surface", kind);
    let before = doc.clone();
    assert_eq!(acis::pending(&body), 1);
    assert_eq!(acis::lower(&body, &mut doc), Ok(1));
    assert_eq!(doc.record(1), before.record(1));
    assert_eq!(doc.record(2), before.record(2));
    assert_eq!(doc.record(5), before.record(5));
    assert_eq!(doc.record(6), before.record(6));
    assert_eq!(doc.record(4).unwrap().attribute, SatPointer::new(0));
    assert_eq!(doc.record(4).unwrap().subtype_id, 21);
    assert_eq!(doc.record(4).unwrap().raw_text.as_deref(), Some("raw-4"));
    trace(label, &doc);
}

fn main() {
    let frame = Plane::from_axes([1.0, 2.0, 3.0], [0.0, 1.0, 0.0], [-1.0, 0.0, 0.0]);
    lower_surface("cylinder", Surface::Cylinder(Cylinder { base: frame, radius: 2.5 }), "cone-surface");
    lower_surface("cone", Surface::Cone(Cone { base: frame, radius: 3.0, half_angle: 0.3 }), "cone-surface");
    lower_surface("sphere", Surface::Sphere(Sphere { frame, radius: 2.25 }), "sphere-surface");
    lower_surface("torus", Surface::Torus(Torus { frame, major_radius: 4.0, minor_radius: 1.25 }), "torus-surface");
    lower_curve("circle", Curve3::Circle(Circle3 { plane: frame, radius: 2.5 }), "ellipse-curve");
    lower_curve("ellipse", Curve3::Ellipse(Ellipse3 { plane: frame, major_radius: 3.0, minor_radius: 1.5 }), "ellipse-curve");

    let sphere = surface_body(Surface::Sphere(Sphere { frame, radius: 2.25 }));
    let mut wrong = document("sphere-surface", "straight-curve");
    wrong.record_mut(2).unwrap().entity_type = "torus-surface".into();
    let before = wrong.clone();
    assert_eq!(acis::lower(&sphere, &mut wrong), Err(Unwritable::WrongRecord { found: "torus-surface".into(), expected: "sphere-surface".into() }));
    assert_eq!(wrong, before);
    println!("wrong_surface;torus-surface;sphere-surface");

    let circle = curve_body(Curve3::Circle(Circle3 { plane: frame, radius: 2.5 }));
    let mut wrong = document("plane-surface", "ellipse-curve");
    wrong.record_mut(4).unwrap().entity_type = "straight-curve".into();
    let before = wrong.clone();
    assert_eq!(acis::lower(&circle, &mut wrong), Err(Unwritable::WrongRecord { found: "straight-curve".into(), expected: "ellipse-curve".into() }));
    assert_eq!(wrong, before);
    println!("wrong_curve;straight-curve;ellipse-curve");

    let mut wrong_owner = document("sphere-surface", "straight-curve");
    wrong_owner.record_mut(1).unwrap().entity_type = "edge".into();
    let before = wrong_owner.clone();
    assert_eq!(acis::lower(&sphere, &mut wrong_owner), Err(Unwritable::MissingSource(1)));
    assert_eq!(wrong_owner, before);
    println!("wrong_owner;1");

    let mut wrong_curve_owner = document("plane-surface", "ellipse-curve");
    wrong_curve_owner.record_mut(3).unwrap().entity_type = "face".into();
    let before = wrong_curve_owner.clone();
    assert_eq!(acis::lower(&circle, &mut wrong_curve_owner), Err(Unwritable::MissingSource(3)));
    assert_eq!(wrong_curve_owner, before);
    println!("wrong_curve_owner;3");

    let mut missing_target = document("sphere-surface", "straight-curve");
    missing_target.record_mut(1).unwrap().tokens[5] = pointer(999);
    let before = missing_target.clone();
    assert_eq!(acis::lower(&sphere, &mut missing_target), Err(Unwritable::MissingSource(1)));
    assert_eq!(missing_target, before);
    println!("missing_target;1");

    let mut missing_source_body = sphere.clone();
    let face = missing_source_body.faces.keys().next().unwrap();
    missing_source_body.faces.get_mut(face).unwrap().provenance = Provenance::Dirty(SourceRef::new(999));
    let mut missing_source = document("sphere-surface", "straight-curve");
    let before = missing_source.clone();
    assert_eq!(acis::lower(&missing_source_body, &mut missing_source), Err(Unwritable::MissingSource(999)));
    assert_eq!(missing_source, before);
    println!("missing_source;999");
}
