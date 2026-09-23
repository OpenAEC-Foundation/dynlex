// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatPointer, SatRecord, SatToken, SatVersion, Sense};

fn main() {
    let mut document = SatDocument::new();
    assert_eq!((document.header.version.major, document.header.version.minor), (7, 0));
    assert_eq!((document.header.num_records, document.header.num_bodies), (0, 0));
    assert!(!document.header.has_history);
    assert_eq!(document.header.spatial_resolution, 10.0);
    assert_eq!(SatVersion::new(4, 1, 2), SatVersion { major: 4, minor: 1, patch: 2 });
    println!("header_defaults");

    let face = SatRecord::new(41, "prefix-face");
    let body = SatRecord::new(73, "body");
    assert!(face.attribute.is_null());
    assert_eq!(face.subtype_id, -1);
    assert!(face.is_a("face"));
    assert!(!face.is_a("prefix"));
    assert_eq!(document.add_record(face), 0);
    assert_eq!(document.add_record(body), 1);
    assert_eq!(document.header.num_records, 2);
    println!("append_reassigns_ids_and_updates_header");

    document.records.swap(0, 1);
    assert_eq!(document.record(0).unwrap().entity_type, "prefix-face");
    assert_eq!(document.record(1).unwrap().entity_type, "body");
    assert!(document.record(2).is_none());
    println!("reordered_records_resolve_by_id");

    assert!(document.resolve(SatPointer::NULL).is_none());
    assert!(document.resolve(SatPointer::new(-7)).is_none());
    assert!(document.resolve(SatPointer::new(19)).is_none());
    assert!(document.resolve(SatPointer::new(0)).is_some());
    println!("null_and_missing_pointers_are_absent");

    let mut payload = SatRecord::new(5, "edge");
    payload.tokens.extend([
        SatToken::Pointer(SatPointer::NULL),
        SatToken::Float(11.0),
        SatToken::Position(2.0, 3.0, 4.0),
        SatToken::Integer(5),
        SatToken::Pointer(SatPointer::new(0)),
        SatToken::Enum("reversed".into()),
        SatToken::Integer(0),
    ]);
    assert_eq!(payload.token_pointer(4), Some(SatPointer::new(0)));
    assert_eq!(payload.token_pointer(3), None);
    assert_eq!(payload.nth_pointer(0), Some(SatPointer::NULL));
    assert_eq!(payload.nth_pointer(1), Some(SatPointer::new(0)));
    assert_eq!(payload.nth_pointer(2), None);
    println!("token_positions_and_pointer_ordinals");

    assert_eq!(payload.token_float(0), None);
    for (slot, value) in [(1, 11.0), (2, 2.0), (3, 3.0), (4, 4.0), (5, 5.0)] {
        assert_eq!(payload.token_float(slot), Some(value));
    }
    assert_eq!(payload.token_float(6), None);
    println!("packed_and_scalar_numeric_slots");

    assert_eq!(payload.token_sense(5), Sense::Reversed);
    assert_eq!(payload.token_sense(6), Sense::Forward);
    assert_eq!(payload.token_sense(20), Sense::Forward);
    payload.tokens.push(SatToken::Integer(1));
    assert_eq!(payload.token_sense(7), Sense::Reversed);
    println!("numeric_and_keyword_orientation");

    let mut raw_bytes = vec![0, 255, 127];
    payload.tokens.push(SatToken::Sab { tag: 224, data: raw_bytes.clone() });
    match payload.token(8) {
        Some(SatToken::Sab { tag, data }) => {
            assert_eq!(*tag, 224);
            assert_eq!(data.as_slice(), &[0, 255, 127]);
        }
        other => panic!("unexpected binary token: {other:?}"),
    }
    assert_eq!(payload.token_float(9), Some(1.0));
    assert_eq!(payload.token_float(10), None);
    raw_bytes[1] = 3;
    assert!(matches!(payload.token(8), Some(SatToken::Sab { data, .. }) if data[1] == 255));
    println!("unknown_binary_token_preserves_bytes");

    payload.tokens.push(SatToken::Sab { tag: 2, data: vec![1] });
    payload.tokens.push(SatToken::Sab { tag: 12, data: vec![0, 0, 0, 0] });
    assert_eq!(payload.token_sense(9), Sense::Reversed);
    assert_eq!(payload.token_float(11), Some(1.0));
    assert_eq!(payload.nth_pointer(2), None);
    println!("sab_numeric_orientation_keeps_pointer_kind");

    let xyz = [1.0_f64, 2.0, 3.0]
        .into_iter()
        .flat_map(f64::to_le_bytes)
        .collect();
    let uv = [4.0_f64, 5.0]
        .into_iter()
        .flat_map(f64::to_le_bytes)
        .collect();
    let mut vectors = SatRecord::new(6, "transform");
    vectors.tokens.extend([
        SatToken::Pointer(SatPointer::NULL),
        SatToken::Sab { tag: 19, data: xyz },
        SatToken::Integer(9),
        SatToken::Sab { tag: 22, data: uv },
        SatToken::Float(7.0),
    ]);
    assert_eq!(vectors.token_float(0), None);
    for (slot, value) in [(1, 1.0), (2, 2.0), (3, 3.0), (4, 9.0), (5, 4.0), (6, 5.0), (7, 7.0)] {
        assert_eq!(vectors.token_float(slot), Some(value));
    }
    println!("sab_coordinate_tokens_expand_numeric_slots");

    let mut malformed = SatRecord::new(7, "point");
    malformed.tokens.push(SatToken::Sab { tag: 19, data: vec![0, 1] });
    malformed.tokens.push(SatToken::Float(6.0));
    assert_eq!(malformed.token_float(0), None);
    assert_eq!(malformed.token_float(1), Some(6.0));
    assert!(matches!(malformed.token(0), Some(SatToken::Sab { tag: 19, data }) if data.as_slice() == [0, 1]));
    println!("malformed_binary_vector_remains_opaque");

    let mut widths = SatRecord::new(8, "value");
    widths.tokens.extend([
        SatToken::Sab { tag: 2, data: vec![255] },
        SatToken::Sab { tag: 3, data: vec![0, 128] },
        SatToken::Sab { tag: 4, data: vec![0, 0, 0, 128] },
        SatToken::Sab { tag: 21, data: vec![255; 4] },
        SatToken::Sab { tag: 23, data: vec![255; 8] },
        SatToken::Sab { tag: 5, data: 1.5_f32.to_le_bytes().to_vec() },
        SatToken::Sab { tag: 6, data: 2.5_f64.to_le_bytes().to_vec() },
    ]);
    for (slot, value) in [(0, -1.0), (1, -32768.0), (2, -2147483648.0), (3, -1.0), (4, -1.0), (5, 1.5), (6, 2.5)] {
        assert_eq!(widths.token_float(slot), Some(value));
    }
    assert_eq!(widths.token_sense(0), Sense::Reversed);
    assert_eq!(widths.token_integer(5), None);
    println!("sab_numeric_widths_and_signs");

    let mut variants = SatRecord::new(9, "value");
    variants.tokens.extend([
        SatToken::Ident("forward".into()),
        SatToken::String("label".into()),
        SatToken::True,
        SatToken::False,
        SatToken::Terminator,
    ]);
    assert!(matches!(variants.token(0), Some(SatToken::Ident(s)) if s == "forward"));
    assert!(matches!(variants.token(1), Some(SatToken::String(s)) if s == "label"));
    assert!(matches!(variants.token(2), Some(SatToken::True)));
    assert!(matches!(variants.token(3), Some(SatToken::False)));
    assert!(matches!(variants.token(4), Some(SatToken::Terminator)));
    assert_eq!(variants.token_float(4), None);
    println!("other_token_variants_keep_their_kind");

    let snapshot = document.clone();
    document.records[0] = SatRecord::new(1, "changed");
    assert_eq!(snapshot.record(1).unwrap().entity_type, "body");
    assert_eq!(snapshot.record(0).unwrap().entity_type, "prefix-face");
    let payload_copy = payload.clone();
    if let SatToken::Sab { data, .. } = &mut payload.tokens[8] {
        data[1] = 5;
    } else {
        panic!("binary token changed kind");
    }
    assert!(matches!(payload_copy.token(8), Some(SatToken::Sab { data, .. }) if data[1] == 255));
    println!("explicit_clone_preserves_ids_and_binary_payload");

    let mut owned_document = SatDocument::new();
    let mut external_record = SatRecord::new(99, "value");
    external_record.tokens.push(SatToken::Sab { tag: 224, data: vec![0, 255] });
    let mut external_copy = external_record.clone();
    let owned_id = owned_document.add_record(external_record);
    if let SatToken::Sab { data, .. } = &mut external_copy.tokens[0] {
        data[1] = 7;
    } else {
        panic!("binary token changed kind");
    }
    assert!(matches!(owned_document.record(owned_id as usize).unwrap().token(0), Some(SatToken::Sab { data, .. }) if data[1] == 255));
    println!("append_takes_an_independent_record_value");

    assert_eq!(SatToken::Float(9223372036854775808.0).as_integer(), Some(i64::MAX));
    assert_eq!(SatToken::Float(-9223372036854775808.0).as_integer(), Some(i64::MIN));
    assert_eq!(SatToken::Float(18446744073709551616.0).as_integer(), None);
    println!("exact_integer_float_boundary_matches_codec");
}
