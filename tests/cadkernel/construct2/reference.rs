// SPDX-License-Identifier: MPL-2.0
use std::io::Read;
use geom2d::{construct::*, arc_fit::fit_arc_chain, Curve};
fn take(input: &mut std::str::SplitWhitespace<'_>) -> f64 { input.next().unwrap().parse().unwrap() }
fn point(input: &mut std::str::SplitWhitespace<'_>) -> [f64;2] { [take(input),take(input)] }
fn emit(value:f64) { println!("{value:.17e}"); }
fn emit_point(value:[f64;2]) { for x in value { emit(x); } }
fn emit_arc(value:Option<geom2d::Arc>) {
    emit(value.is_some() as u8 as f64);
    if let Some(arc)=value {
        emit_point(arc.centre);emit(arc.radius);emit(arc.start_angle);emit(arc.end_angle);emit(arc.sweep());
        let curve=Curve::Arc(arc);
        for i in 0..5 {let t=(i as f64-1.)/2.;emit_point(curve.point_at(t));emit_point(curve.tangent_at(t));}
    }
}
fn main() {
    let mut text=String::new();std::io::stdin().read_to_string(&mut text).unwrap();
    let mut input=text.split_whitespace();let count=take(&mut input) as usize;
    for id in 0..count {
        println!("case {id}");
        let kind=take(&mut input) as usize;
        if kind<7 {
            let first=point(&mut input);let middle=point(&mut input);let last=point(&mut input);
            let a=take(&mut input);let b=take(&mut input);let c=take(&mut input);
            match kind {
                0=>emit_arc(bounded_arc(first,a,b,c)),
                1=>emit_arc(arc_from_endpoints_angle(first,last,a)),
                2=>emit_arc(arc_from_endpoints_radius(first,last,a)),
                3=>emit_arc(arc_from_sagitta(first,last,a)),
                4=>emit_arc(arc_from_start_tangent(first,middle,last,b!=0.)),
                5=>emit_arc(arc_through_points(first,middle,last)),
                _=>{let result=arc_sweep_from_chord(a,b);emit(result.is_some() as u8 as f64);if let Some(x)=result{emit(x);}},
            }
        } else {
            let n=take(&mut input) as usize;let closed=take(&mut input)!=0.;let m=take(&mut input) as usize;
            let points:Vec<_>=(0..n).map(|_|point(&mut input)).collect();
            let directions:Vec<_>=(0..m).map(|_|{let specified=take(&mut input)!=0.;let p=point(&mut input);specified.then_some(p)}).collect();
            let result=fit_arc_chain(&points,closed,&directions);emit(result.is_some() as u8 as f64);
            if let Some(vertices)=result {emit(vertices.len() as f64);for v in vertices {emit_point(v.point);emit(v.bulge);emit(v.source as f64);emit(v.fraction);emit(v.inserted as u8 as f64);}}
        }
    }
    assert!(input.next().is_none());
}

