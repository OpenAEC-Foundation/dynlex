// SPDX-License-Identifier: MPL-2.0
// Calls unchanged pinned production functions with runtime arguments.
fn vector(v: &[f64], i: usize) -> [f64;3] { [v[i],v[i+1],v[i+2]] }
fn arc(v: &[f64], i: usize) -> space::arc_union::CircularArc {
    space::arc_union::CircularArc { center: vector(v,i), normal: vector(v,i+3),
        radius: v[i+6], start: v[i+7], end: v[i+8] }
}
fn line_kind(v: f64) -> space::line_union::LineUnionKind {
    use space::line_union::LineUnionKind::*;
    [Duplicate,Overlap,EndToEnd][v as usize]
}
fn arc_kind(v: f64) -> space::arc_union::ArcUnionKind {
    use space::arc_union::ArcUnionKind::*;
    [Duplicate,Overlap,EndToEnd][v as usize]
}
fn emit(v: f64) { println!("{v:.17e}"); }
fn main() {
    use space::{arc_union::*,line_union::*};
    let v: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    match v[0] as usize {
        0 => {
            let joined=circular_arc_union(arc(&v,2),arc(&v,11),v[1]);
            emit(joined.is_some() as u8 as f64);
            if let Some(j)=joined { emit(j.start);emit(j.end);emit(j.full_circle as u8 as f64);emit(j.kind as u8 as f64); }
        }
        1 => emit(circle_contains_arc(vector(&v,1),vector(&v,4),v[7],arc(&v,8)) as u8 as f64),
        2 => {
            let joined=line_union([vector(&v,2),vector(&v,5)],[vector(&v,8),vector(&v,11)],v[1]);
            emit(joined.is_some() as u8 as f64);
            if let Some(j)=joined { for x in j.start.into_iter().chain(j.end) { emit(x); } emit(j.kind as u8 as f64); }
        }
        3 => {
            let points=(0..v[2] as usize).map(|i|vector(&v,3+3*i)).collect::<Vec<_>>();
            let kept=simplify_linear_chain(&points,v[1]);
            emit(kept.len() as f64);for i in kept { emit(i as f64); }
        }
        4 => {
            let a=LineUnion{start:vector(&v,1),end:vector(&v,4),kind:line_kind(v[7])};
            let b=LineUnion{start:vector(&v,8),end:vector(&v,11),kind:line_kind(v[14])};
            emit((a==b) as u8 as f64);emit((a.kind==b.kind) as u8 as f64);
            emit((arc_kind(v[7])==arc_kind(v[14])) as u8 as f64);
        }
        _ => unreachable!(),
    }
}
