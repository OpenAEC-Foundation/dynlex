// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{SatDocument, SatRecord, SatToken};
use cadkernel::{acis, brep::make};

fn bits(value: f64) -> String {
    let word = if value == 0.0 { 0 } else { value.to_bits() };
    format!("{}:{}", (word >> 32) as u32, word as u32)
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
                other => panic!("unexpected token in sphere append: {other:?}"),
            }
        }
        println!("{line}");
    }
}

fn main() {
    let mut document = SatDocument::new();
    assert_eq!(document.add_record(SatRecord::new(-1, "seed")), 0);
    let first = make::sphere([1.0, 2.0, 3.0], 2.0).unwrap();
    let written = acis::append(&first, &mut document).unwrap();
    assert_eq!((written.body, written.records), (14, 14));
    println!("append;{};{}", written.body, written.records);
    let second = make::sphere([-3.0, 1.0, 0.5], 1.5).unwrap();
    let written = acis::append(&second, &mut document).unwrap();
    assert_eq!((written.body, written.records), (28, 14));
    assert_eq!(document.record_count(), 29);
    println!("append;{};{}", written.body, written.records);
    trace(&document);
}
