// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken};
use cadkernel::{
    acis::{self, Unwritable},
    brep::{make, Body, Circle3, Curve3, Ellipse3, Line3, Provenance, SourceRef, Surface},
    geom2d::NurbsCurve,
    space::{NurbsCurve3, NurbsSurface3, Plane},
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

fn audit(label: &str, body: &Body, document: &mut SatDocument) {
    let pending = acis::pending(body);
    let result = match acis::lower(body, document) {
        Ok(count) => format!("ok:{count}"),
        Err(Unwritable::MissingSource(source)) => format!("missing:{source}"),
        Err(Unwritable::Surface) => "surface".into(),
        Err(Unwritable::Curve) => "curve".into(),
        Err(Unwritable::WrongRecord { found, expected }) => format!("wrong:{found}:{expected}"),
    };
    println!("audit;{label};{pending};{result}");
    trace(document);
}

fn fresh_body() -> Body {
    let mut body = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    clean(&mut body);
    body
}

fn dirty_face(body: &mut Body, source: u32, surface: Surface) {
    let key = body.faces.keys().next().unwrap();
    let support = body.faces.get(key).unwrap().surface;
    body.faces.get_mut(key).unwrap().provenance = Provenance::Dirty(SourceRef::new(source));
    *body.surfaces.get_mut(support).unwrap() = surface;
}

fn dirty_edge(body: &mut Body, source: u32, curve: Curve3) {
    let key = body.edges.keys().next().unwrap();
    let support = body.edges.get(key).unwrap().curve;
    body.edges.get_mut(key).unwrap().provenance = Provenance::Dirty(SourceRef::new(source));
    *body.curves.get_mut(support).unwrap() = curve;
}

fn main() {
    let mut body = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    assert_eq!(acis::pending(&body), 26);
    println!("pending_synth;26");
    clean(&mut body);
    assert_eq!(acis::pending(&body), 0);
    let mut sample_document = document();
    let before = sample_document.clone();
    assert_eq!(acis::lower(&body, &mut sample_document), Ok(0));
    assert_eq!(sample_document, before);
    println!("pending_clean;0");
    dirty_three(&mut body);
    assert_eq!(acis::pending(&body), 3);
    println!("pending_dirty;3");
    assert_eq!(acis::lower(&body, &mut sample_document), Ok(3));
    assert_eq!(sample_document.record(0).unwrap().attribute, SatPointer::new(3));
    assert_eq!(sample_document.record(0).unwrap().subtype_id, 12);
    assert_eq!(sample_document.record(0).unwrap().raw_text.as_deref(), Some("raw point"));
    println!("lower_written;3");
    trace(&sample_document);

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

    let mut synthesized = make::cuboid([0.0; 3], [1.0, 2.0, 3.0]).unwrap();
    let mut empty = SatDocument::new();
    audit("synth_only", &synthesized, &mut empty);
    clean(&mut synthesized);
    let second_face = synthesized.faces.keys().nth(1).unwrap();
    synthesized.faces.get_mut(second_face).unwrap().provenance = Provenance::Synthesized;
    dirty_edge(&mut synthesized, 1, Curve3::Line(Line3 { origin: [2.0, 3.0, 4.0], direction: [5.0, 0.0, 0.0] }));
    audit("synth_mixed", &synthesized, &mut document());

    let nurbs_surface = NurbsSurface3::new(1, 1,
        vec![vec![[0.0, 0.0, 0.0], [1.0, 0.0, 0.0]], vec![[0.0, 1.0, 0.0], [1.0, 1.0, 0.0]]],
        vec![], vec![], None).unwrap();
    let mut unsupported = fresh_body();
    dirty_face(&mut unsupported, 99, Surface::Nurbs(nurbs_surface));
    audit("nurbs_surface_before_source", &unsupported, &mut document());

    let nurbs_curve = NurbsCurve3::new(1, vec![[0.0; 3], [1.0, 0.0, 0.0]], vec![], None).unwrap();
    let mut unsupported = fresh_body();
    dirty_edge(&mut unsupported, 99, Curve3::Nurbs(nurbs_curve));
    audit("nurbs_curve_before_source", &unsupported, &mut document());

    let planar_curve = NurbsCurve::new(1, vec![[0.0, 0.0], [1.0, 0.0]], vec![], None).unwrap();
    let mut unsupported = fresh_body();
    dirty_edge(&mut unsupported, 99, Curve3::PlanarSpline { plane: Plane::XY, curve: planar_curve });
    audit("planar_spline_before_source", &unsupported, &mut document());

    let invalid_plane = Plane::from_axes([0.0; 3], [1.0, 0.0, 0.0], [2.0, 0.0, 0.0]);
    let mut invalid = fresh_body();
    dirty_edge(&mut invalid, 99, Curve3::Circle(Circle3 { plane: invalid_plane, radius: 2.0 }));
    audit("invalid_circle_before_source", &invalid, &mut document());
    let mut invalid = fresh_body();
    dirty_edge(&mut invalid, 99, Curve3::Ellipse(Ellipse3 { plane: invalid_plane, major_radius: 2.0, minor_radius: 1.0 }));
    audit("invalid_ellipse_before_source", &invalid, &mut document());

    let mut missing_surface = fresh_body();
    dirty_face(&mut missing_surface, 5, Surface::Plane(Plane::XY));
    let face = missing_surface.faces.keys().next().unwrap();
    let support = missing_surface.faces.get(face).unwrap().surface;
    missing_surface.surfaces.remove(support).unwrap();
    audit("missing_surface_node", &missing_surface, &mut document());
    let mut missing_curve = fresh_body();
    dirty_edge(&mut missing_curve, 1, Curve3::Line(Line3 { origin: [2.0, 3.0, 4.0], direction: [5.0, 0.0, 0.0] }));
    let edge = missing_curve.edges.keys().next().unwrap();
    let support = missing_curve.edges.get(edge).unwrap().curve;
    missing_curve.curves.remove(support).unwrap();
    audit("missing_curve_node", &missing_curve, &mut document());

    let mut partial = fresh_body();
    dirty_three(&mut partial);
    let mut failed_edge = document();
    failed_edge.record_mut(6).unwrap().entity_type = "ellipse-curve".into();
    audit("partial_edge", &partial, &mut failed_edge);
    let mut failed_vertex = document();
    failed_vertex.record_mut(0).unwrap().entity_type = "plane-surface".into();
    audit("partial_vertex", &partial, &mut failed_vertex);
    let mut second_face_failure = fresh_body();
    dirty_face(&mut second_face_failure, 5, Surface::Plane(Plane::XY));
    let second_face = second_face_failure.faces.keys().nth(1).unwrap();
    second_face_failure.faces.get_mut(second_face).unwrap().provenance = Provenance::Dirty(SourceRef::new(99));
    audit("partial_second_face", &second_face_failure, &mut document());

    let mut face_only = fresh_body();
    dirty_face(&mut face_only, 5, Surface::Plane(Plane::XY));
    let mut sparse_source = document();
    sparse_source.records[0].index = 99;
    dirty_face(&mut face_only, 99, Surface::Plane(Plane::XY));
    audit("sparse_source", &face_only, &mut sparse_source);
    dirty_face(&mut face_only, 5, Surface::Plane(Plane::XY));
    let mut sparse_target = document();
    sparse_target.records[4].index = 99;
    sparse_target.record_mut(5).unwrap().tokens[5] = pointer(99);
    audit("sparse_target", &face_only, &mut sparse_target);

    let mut duplicate_source = document();
    duplicate_source.records[5].index = 5;
    audit("duplicate_source", &face_only, &mut duplicate_source);

    let mut malformed = document();
    malformed.record_mut(5).unwrap().tokens[5] = SatToken::Ident("bad-pointer".into());
    audit("face_nonpointer", &face_only, &mut malformed);
    let mut malformed = document();
    malformed.record_mut(5).unwrap().tokens[5] = pointer(-1);
    audit("face_null", &face_only, &mut malformed);

    let mut edge_only = fresh_body();
    dirty_edge(&mut edge_only, 1, Curve3::Line(Line3 { origin: [2.0, 3.0, 4.0], direction: [5.0, 0.0, 0.0] }));
    let mut malformed = document();
    malformed.record_mut(1).unwrap().tokens[6] = SatToken::Ident("bad-pointer".into());
    audit("edge_nonpointer", &edge_only, &mut malformed);
    let mut malformed = document();
    malformed.record_mut(1).unwrap().tokens[6] = pointer(-1);
    audit("edge_null", &edge_only, &mut malformed);

    let mut vertex_only = fresh_body();
    let vertex_key = vertex_only.vertices.keys().next().unwrap();
    vertex_only.vertices.get_mut(vertex_key).unwrap().provenance = Provenance::Dirty(SourceRef::new(4));
    let mut malformed = document();
    malformed.record_mut(4).unwrap().tokens[3] = SatToken::Ident("bad-pointer".into());
    audit("vertex_nonpointer", &vertex_only, &mut malformed);
    let mut malformed = document();
    malformed.record_mut(4).unwrap().tokens[3] = pointer(-1);
    audit("vertex_null", &vertex_only, &mut malformed);

    let mut derived = document();
    derived.record_mut(5).unwrap().entity_type = "vendor-face".into();
    derived.record_mut(1).unwrap().entity_type = "vendor-edge".into();
    derived.record_mut(4).unwrap().entity_type = "vendor-vertex".into();
    derived.record_mut(2).unwrap().sub_type = Some("retained".into());
    audit("derived_owner", &partial, &mut derived);
    assert_eq!(derived.record(2).unwrap().sub_type.as_deref(), Some("retained"));
}
