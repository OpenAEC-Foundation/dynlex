// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatRecord, SatToken};
use cadkernel::{acis, brep::make};

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
                SatToken::Ident(value) => line.push_str(&format!(";i{}", value)),
                other => panic!("unexpected token in faceted trace: {other:?}"),
            }
        }
        println!("{line}");
    }
}

fn main() {
    let mut document = SatDocument::new();
    assert_eq!(document.add_record(SatRecord::new(-1, "seed")), 0);
    let first = make::cuboid([0.0, 0.0, 0.0], [1.0, 2.0, 4.0]).unwrap();
    let written = acis::append(&first, &mut document).unwrap();
    assert_eq!((written.body, written.records), (85, 85));
    let second = make::cuboid([10.0, -2.0, 0.5], [3.0, 5.0, 7.0]).unwrap();
    let written = acis::append(&second, &mut document).unwrap();
    assert_eq!((written.body, written.records), (170, 85));
    assert_eq!(document.record_count(), 171);
    trace(&document);
}
