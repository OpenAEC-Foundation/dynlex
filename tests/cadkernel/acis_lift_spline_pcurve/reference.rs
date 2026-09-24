// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{
    acis,
    brep::{make, Body, Curve3, Key, Provenance, Surface},
    geom2d::Curve as Curve2,
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

fn bounded_gap(value: f64) -> String {
    assert!(value < 1e-5, "vertex gap {value}");
    "within-1e-5".into()
}

fn vector(value: [f64; 3]) -> String {
    format!("{},{},{}", bits(value[0]), bits(value[1]), bits(value[2]))
}

fn knot_bits(values: &[f64]) -> String {
    values.iter().map(|value| bits(*value)).collect::<Vec<_>>().join(",")
}

fn controls2(values: &[[f64; 2]]) -> String {
    values.iter().map(|value| format!("{},{}", bits(value[0]), bits(value[1]))).collect::<Vec<_>>().join("|")
}

fn controls3(values: &[[f64; 3]]) -> String {
    values.iter().map(|value| vector(*value)).collect::<Vec<_>>().join("|")
}

fn refs<T>(items: &[Key<T>], mut id: impl FnMut(Key<T>) -> i64) -> String {
    items.iter().map(|key| id(*key).to_string()).collect::<Vec<_>>().join(",")
}

fn trace(label: &str, body: &Body) {
    let flaws = body.validate();
    assert!(flaws.is_empty(), "{label}: {flaws:?}");
    assert!(body.worst_vertex_gap() < 1e-5);
    println!("case;{label}");
    println!(
        "body;{};{};{};{};{};{};{};{};{};{};{};{};{}",
        source(body.provenance), body.vertices.len(), body.edges.len(), body.coedges.len(),
        body.loops.len(), body.faces.len(), body.shells.len(), body.lumps.len(),
        body.surfaces.len(), body.curves.len(), flaws.len(), body.euler_characteristic(),
        bounded_gap(body.worst_vertex_gap()),
    );
    println!("roots;{}", refs(&body.roots, |key| source(body.lumps.get(key).unwrap().provenance)));
    for (key, value) in body.surfaces.iter() {
        let Surface::Plane(plane) = value else { panic!("expected plane") };
        println!("surface;{};{};{};{}", key.slot(), vector(plane.origin), vector(plane.x_axis), vector(plane.y_axis));
    }
    for (key, value) in body.curves.iter() {
        match value {
            Curve3::Line(line) => println!("curve;{};{};{}", key.slot(), vector(line.origin), vector(line.direction)),
            Curve3::Nurbs(curve) => println!("curve;{};nurbs;{};{};{};{};{}", key.slot(),
                curve.degree(), u8::from(curve.periodicity()), knot_bits(curve.knots()),
                controls3(curve.control_points()), knot_bits(curve.weights())),
            other => panic!("unexpected curve: {other:?}"),
        }
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
        if let Some(Curve2::Nurbs(curve)) = &value.pcurve {
            println!("pcurve;{};{};{};{};{}", key.slot(), curve.degree(),
                knot_bits(curve.knots()), controls2(curve.control_points()), knot_bits(curve.weights()));
        }
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
    let first_edge = body.edges.iter().next().unwrap().1;
    let endpoints = [
        body.vertices.get(first_edge.start).unwrap().point,
        body.vertices.get(first_edge.end).unwrap().point,
    ];
    let mut document = SatDocument::new();
    document.add_record(SatRecord::new(-1, "seed"));
    let written = acis::append(&body, &mut document).unwrap();
    assert_eq!((written.body, written.records), (85, 85));
    assert_eq!(document.record(35).unwrap().entity_type, "edge");
    assert_eq!(document.record(47).unwrap().entity_type, "coedge");
    assert_eq!(document.record(29).unwrap().entity_type, "plane-surface");
    let curve_id = document.add_spline_curve(true, 1, false, &[(0.0, 2), (1.0, 2)],
        &endpoints, Some(&[2.0, 3.0]), 0.0);
    assert_eq!(curve_id, 86);
    document.record_mut(35).unwrap().tokens[6] = SatToken::Pointer(SatPointer::new(curve_id));
    let pcurve_id = document.add_pcurve(true, 1, false, &[(0.0, 2), (1.0, 2)],
        &[[0.0, 0.0], [1.0, 0.0]], Some(&[2.0, 3.0]), 0.0, 29, (0.25, -0.5));
    assert_eq!(pcurve_id, 87);
    document.record_mut(47).unwrap().tokens[7] = SatToken::Pointer(SatPointer::new(pcurve_id));
    let (lifted, loss) = acis::lift_body(&document, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    assert_eq!(lifted.curves.iter().filter(|(_, value)| matches!(value, Curve3::Nurbs(_))).count(), 1);
    assert_eq!(lifted.coedges.iter().filter(|(_, value)| value.pcurve.is_some()).count(), 1);
    trace("weighted", &lifted);

    let mut reordered = document.clone();
    reordered.records.swap(0, 85);
    reordered.records.swap(1, 87);
    reordered.records.swap(35, 47);
    assert_ne!(slot(&reordered, 87), 87);
    let (lifted, loss) = acis::lift_body(&reordered, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    trace("reordered", &lifted);

    let mut invalid_pcurve = reordered.clone();
    invalid_pcurve.record_mut(pcurve_id as usize).unwrap().tokens[15] = SatToken::Float(0.0);
    let (lifted, loss) = acis::lift_body(&invalid_pcurve, written.body as usize).unwrap();
    assert!(loss.is_empty(), "{loss:?}");
    assert!(lifted.coedges.iter().all(|(_, value)| value.pcurve.is_none()));
    trace("invalid-pcurve-skipped", &lifted);
}
