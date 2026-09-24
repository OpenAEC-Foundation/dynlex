// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{acis, brep::{make, Body, Curve3, Key, Provenance, Surface}};

fn source(value: Provenance) -> i64 {
    match value {
        Provenance::Clean(id) => id.index() as i64,
        Provenance::Dirty(id) => -(id.index() as i64) - 2,
        Provenance::Synthesized => -1,
    }
}
fn bits(value: f64) -> String {
    let word = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", word >> 32, word as u32)
}
fn vector(value: [f64; 3]) -> String {
    format!("{},{},{}", bits(value[0]), bits(value[1]), bits(value[2]))
}
fn refs<T>(keys: &[Key<T>], mut record: impl FnMut(Key<T>) -> i64) -> String {
    keys.iter().map(|key| record(*key).to_string()).collect::<Vec<_>>().join(",")
}

fn trace(label: &str, body: &Body) {
    let flaws = body.validate();
    assert!(flaws.is_empty(), "{label}: {flaws:?}");
    assert!(body.worst_vertex_gap() < 1e-12);
    println!("case;{label}");
    println!("body;{};{};{};{};{};{};{};{};{};{};{};{};{}",
        source(body.provenance), body.vertices.len(), body.edges.len(), body.coedges.len(),
        body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
        body.surfaces.len(), body.curves.len(), flaws.len(), body.euler_characteristic(),
        bits(body.worst_vertex_gap()));
    println!("roots;{}", refs(&body.roots, |key| source(body.lumps.get(key).unwrap().provenance)));
    for (key, value) in body.surfaces.iter() {
        let Surface::Sphere(value) = value else { panic!("expected sphere") };
        println!("surface;{};{};{};{};{}", key.slot(), vector(value.frame.origin),
            vector(value.frame.x_axis), vector(value.frame.y_axis), bits(value.radius));
    }
    for (key, value) in body.curves.iter() {
        let Curve3::Circle(value) = value else { panic!("expected circle") };
        println!("curve;{};{};{};{};{}", key.slot(), vector(value.plane.origin),
            vector(value.plane.x_axis), vector(value.plane.y_axis), bits(value.radius));
    }
    for (key, value) in body.vertices.iter() {
        println!("vertex;{};{};{}", key.slot(), source(value.provenance), vector(value.point));
    }
    for (key, value) in body.edges.iter() {
        println!("edge;{};{};{};{};{};{};{};{}", key.slot(), source(value.provenance), value.curve.slot(),
            source(body.vertices.get(value.start).unwrap().provenance),
            source(body.vertices.get(value.end).unwrap().provenance), bits(value.start_parameter),
            bits(value.end_parameter), refs(&value.coedges, |key| source(body.coedges.get(key).unwrap().provenance)));
    }
    for (key, value) in body.coedges.iter() {
        println!("coedge;{};{};{};{};{};{}", key.slot(), source(value.provenance),
            source(body.edges.get(value.edge).unwrap().provenance),
            source(body.loops.get(value.owner).unwrap().provenance),
            u8::from(value.forward), u8::from(value.pcurve.is_some()));
    }
    for (key, value) in body.loops.iter() {
        println!("loop;{};{};{};{}", key.slot(), source(value.provenance),
            source(body.faces.get(value.owner).unwrap().provenance),
            refs(&value.coedges, |key| source(body.coedges.get(key).unwrap().provenance)));
    }
    for (key, value) in body.faces.iter() {
        println!("face;{};{};{};{};{};{}", key.slot(), source(value.provenance), value.surface.slot(),
            source(body.shells.get(value.owner).unwrap().provenance), u8::from(value.forward),
            refs(&value.loops, |key| source(body.loops.get(key).unwrap().provenance)));
    }
    for (key, value) in body.shells.iter() {
        println!("shell;{};{};{};{}", key.slot(), source(value.provenance),
            source(body.lumps.get(value.owner).unwrap().provenance),
            refs(&value.faces, |key| source(body.faces.get(key).unwrap().provenance)));
    }
    for (key, value) in body.lumps.iter() {
        println!("lump;{};{};{}", key.slot(), source(value.provenance),
            refs(&value.shells, |key| source(body.shells.get(key).unwrap().provenance)));
    }
}

fn main() {
    let body = make::sphere([1.0, 2.0, 3.0], 2.0).unwrap();
    let mut document = SatDocument::new();
    document.add_record(SatRecord::new(-1, "seed"));
    let written = acis::append(&body, &mut document).unwrap();
    assert_eq!((written.body, written.records), (14, 14));
    let names = ["seed", "point", "vertex", "point", "vertex", "ellipse-curve", "sphere-surface",
        "edge", "coedge", "coedge", "loop", "face", "shell", "lump", "body"];
    for (id, name) in names.iter().enumerate() {
        assert_eq!(document.record(id).unwrap().entity_type, *name);
    }
    let (lifted, loss) = acis::lift_body(&document, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("base", &lifted);

    document.record_mut(7).unwrap().tokens[2] = SatToken::Float(-1.0);
    document.record_mut(7).unwrap().tokens[4] = SatToken::Float(1.0);
    document.records.swap(0, 14);
    document.records.swap(1, 9);
    document.records.swap(6, 11);
    assert_eq!(document.records[0].index, 14);
    let (lifted, loss) = acis::lift_body(&document, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("reordered-stale-parameters", &lifted);

    let mut shifted = document.clone();
    shifted.record_mut(7).unwrap().tokens[2] = SatToken::Float(4.71238898038469);
    shifted.record_mut(7).unwrap().tokens[4] = SatToken::Float(7.853981633974483);
    let (lifted, loss) = acis::lift_body(&shifted, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("shifted-period", &lifted);

    let mut reversed = document.clone();
    reversed.record_mut(11).unwrap().tokens[6] = SatToken::Ident("reversed".into());
    let surface = reversed.record_mut(6).unwrap();
    while surface.tokens.len() < 11 { surface.tokens.push(SatToken::Ident("I".into())); }
    surface.tokens.push(SatToken::Ident("reversed".into()));
    let (lifted, loss) = acis::lift_body(&reversed, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("reversed-surface-and-face", &lifted);

    let mut edge_reversed = document.clone();
    edge_reversed.record_mut(7).unwrap().tokens[7] = SatToken::Ident("reversed".into());
    let (lifted, loss) = acis::lift_body(&edge_reversed, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("reversed-edge", &lifted);

    let mut unsupported = document.clone();
    unsupported.record_mut(6).unwrap().entity_type = "unknown-surface".into();
    let (partial, loss) = acis::lift_body(&unsupported, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 0);
    assert_eq!(loss.surfaces, [6]);

    let mut malformed = document.clone();
    malformed.record_mut(7).unwrap().tokens[6] = SatToken::Pointer(SatPointer::new(999));
    let (partial, _) = acis::lift_body(&malformed, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 1);
    assert_eq!(partial.edges.len(), 0);
    assert_eq!(partial.coedges.len(), 0);

    let mut bad_pole = document.clone();
    bad_pole.record_mut(6).unwrap().tokens[4] = SatToken::Position(0.0, 0.0, 0.0);
    let (partial, loss) = acis::lift_body(&bad_pole, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 0);
    assert_eq!(loss.surfaces, [6]);
}
