// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{
    acis,
    brep::{make, Body, Curve3, Key, Provenance, Surface},
};

fn source(provenance: Provenance) -> i64 {
    match provenance {
        Provenance::Clean(value) => value.index() as i64,
        Provenance::Dirty(value) => -(value.index() as i64) - 2,
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

fn refs<T>(items: &[Key<T>], mut id: impl FnMut(Key<T>) -> i64) -> String {
    items.iter().map(|key| id(*key).to_string()).collect::<Vec<_>>().join(",")
}

fn trace(label: &str, body: &Body) {
    let flaws = body.validate();
    assert!(flaws.is_empty(), "{label}: {flaws:?}");
    assert_eq!(body.worst_vertex_gap(), 0.0);
    println!("case;{label}");
    println!(
        "body;{};{};{};{};{};{};{};{};{};{};{};{};{}",
        source(body.provenance), body.vertices.len(), body.edges.len(), body.coedges.len(),
        body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
        body.surfaces.len(), body.curves.len(), flaws.len(), body.euler_characteristic(),
        bits(body.worst_vertex_gap()),
    );
    println!("roots;{}", refs(&body.roots, |key| source(body.lumps.get(key).unwrap().provenance)));
    for (key, value) in body.surfaces.iter() {
        let Surface::Plane(plane) = value else { panic!("expected plane") };
        println!("surface;{};{};{};{}", key.slot(), vector(plane.origin), vector(plane.x_axis), vector(plane.y_axis));
    }
    for (key, value) in body.curves.iter() {
        let Curve3::Line(line) = value else { panic!("expected line") };
        println!("curve;{};{};{}", key.slot(), vector(line.origin), vector(line.direction));
    }
    for (key, value) in body.vertices.iter() {
        println!("vertex;{};{};{}", key.slot(), source(value.provenance), vector(value.point));
    }
    for (key, value) in body.edges.iter() {
        println!(
            "edge;{};{};{};{};{};{};{};{}",
            key.slot(), source(value.provenance), value.curve.slot(),
            source(body.vertices.get(value.start).unwrap().provenance),
            source(body.vertices.get(value.end).unwrap().provenance),
            bits(value.start_parameter), bits(value.end_parameter),
            refs(&value.coedges, |use_key| source(body.coedges.get(use_key).unwrap().provenance)),
        );
    }
    for (key, value) in body.coedges.iter() {
        println!(
            "coedge;{};{};{};{};{};{}",
            key.slot(), source(value.provenance),
            source(body.edges.get(value.edge).unwrap().provenance),
            source(body.loops.get(value.owner).unwrap().provenance),
            if value.forward { 1 } else { 0 }, if value.pcurve.is_some() { 1 } else { 0 },
        );
    }
    for (key, value) in body.loops.iter() {
        println!(
            "loop;{};{};{};{}", key.slot(), source(value.provenance),
            source(body.faces.get(value.owner).unwrap().provenance),
            refs(&value.coedges, |use_key| source(body.coedges.get(use_key).unwrap().provenance)),
        );
    }
    for (key, value) in body.faces.iter() {
        println!(
            "face;{};{};{};{};{};{}", key.slot(), source(value.provenance), value.surface.slot(),
            source(body.shells.get(value.owner).unwrap().provenance),
            if value.forward { 1 } else { 0 },
            refs(&value.loops, |loop_key| source(body.loops.get(loop_key).unwrap().provenance)),
        );
    }
    for (key, value) in body.shells.iter() {
        println!(
            "shell;{};{};{};{}", key.slot(), source(value.provenance),
            source(body.lumps.get(value.owner).unwrap().provenance),
            refs(&value.faces, |face_key| source(body.faces.get(face_key).unwrap().provenance)),
        );
    }
    for (key, value) in body.lumps.iter() {
        println!("lump;{};{};{}", key.slot(), source(value.provenance),
            refs(&value.shells, |shell_key| source(body.shells.get(shell_key).unwrap().provenance)));
    }
}

fn slot(document: &SatDocument, id: i32) -> usize {
    document.records.iter().position(|record| record.index == id).unwrap()
}

fn main() {
    let body = make::cuboid([0.0; 3], [1.0, 2.0, 4.0]).unwrap();
    let mut document = SatDocument::new();
    document.add_record(SatRecord::new(-1, "seed"));
    let written = acis::append(&body, &mut document).unwrap();
    assert_eq!((written.body, written.records), (85, 85));
    let (base, loss) = acis::lift_body(&document, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("base", &base);

    let edge = document.record_mut(35).unwrap();
    edge.tokens[2] = SatToken::Float(-123.0);
    edge.tokens[4] = SatToken::Float(123.0);
    edge.tokens[7] = SatToken::Ident("reversed".into());
    document.records.swap(0, 85);
    document.records.swap(1, 77);
    document.records.swap(35, 47);
    assert_ne!(slot(&document, written.body), written.body as usize);
    let (reordered, loss) = acis::lift_body(&document, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("reordered", &reordered);

    let mut unsupported = document.clone();
    unsupported.record_mut(29).unwrap().entity_type = "unsupported-surface".into();
    let (partial, loss) = acis::lift_body(&unsupported, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 5);
    assert_eq!(loss.surfaces, [29]);

    let mut wrong = document.clone();
    wrong.record_mut(71).unwrap().entity_type = "marker".into();
    let (partial, loss) = acis::lift_body(&wrong, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 6);
    assert_eq!(loss.broken, [71]);

    let mut missing = document.clone();
    missing.record_mut(77).unwrap().tokens[5] = SatToken::Pointer(SatPointer::new(999));
    let (partial, _) = acis::lift_body(&missing, written.body as usize).unwrap();
    assert_eq!(partial.faces.len(), 5);

    let mut open_ring = document.clone();
    open_ring.record_mut(47).unwrap().tokens[1] = SatToken::Pointer(SatPointer::new(47));
    let (partial, _) = acis::lift_body(&open_ring, written.body as usize).unwrap();
    assert!(!partial.validate().is_empty());

    let mut reversed_face = document.clone();
    reversed_face.record_mut(77).unwrap().tokens[6] = SatToken::Ident("reversed".into());
    let (oriented, loss) = acis::lift_body(&reversed_face, written.body as usize).unwrap();
    assert!(loss.is_empty());
    assert!(!oriented.faces.iter().next().unwrap().1.forward);
    println!("orientation;face77;0");

    let mut tolerant_vertex = document.clone();
    tolerant_vertex.record_mut(2).unwrap().tokens.insert(2, SatToken::Integer(7));
    let (tolerant, loss) = acis::lift_body(&tolerant_vertex, written.body as usize).unwrap();
    assert!(loss.is_empty());
    assert_eq!(tolerant.vertices.len(), 8);
    let point = tolerant.vertices.iter().next().unwrap().1;
    assert_eq!(point.point, [0.0; 3]);
    assert_eq!(source(point.provenance), 2);
    println!("tolerance;vertex2;0");
}
