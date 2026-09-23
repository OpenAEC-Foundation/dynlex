// SPDX-License-Identifier: MPL-2.0
// Includes unmodified modules from the supplied pinned source.
use geom2d::nurbs::{NurbsCurve,Parameterization};
fn emit(x:f64) {println!("{x:.17e}");}
fn flag(x:bool) {emit(if x {1.0} else {0.0});}
fn point(p:[f64;2]) {for x in p {emit(x);}}
fn curve(c:&NurbsCurve,u:f64,t:f64,target:[f64;2],pieces:usize,flags:usize) {
    emit(c.degree() as f64);emit(c.control_points().len() as f64);
    emit(c.knots().len() as f64);emit(c.weights().len() as f64);flag(c.is_rational());
    for p in c.control_points() {point(*p);}
    for x in c.knots() {emit(*x);} for x in c.weights() {emit(*x);}
    let (a,b)=c.domain();emit(a);emit(b);
    if flags>=1 {point(c.point_at_knot(u));point(c.point_at(t));point(c.derivative_at_knot(u));point(c.derivative_at(t));flag(c.is_closed());}
    if flags>=2 {emit(c.parameter_at(target));}
    if flags>=3 {let p=c.tessellate(pieces);emit(p.len() as f64);for p in p {point(p);}}
}
fn main() {
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let (ctor,degree,n,nk,nw,tr)=(a[0] as usize,a[1] as usize,a[2] as usize,a[3] as usize,a[4] as usize,a[5] as usize);
    let (amount,last,u,t)=(a[6],a[7],a[8],a[9]);let pieces=a[10] as usize;
    let spacing=match a[11] as usize {0=>Parameterization::Uniform,1=>Parameterization::Centripetal,2=>Parameterization::Chord,_=>panic!("spacing")};
    let flags=a[14] as usize;let mut at=15;
    let points:Vec<[f64;2]>=(0..n).map(|_|{let p=[a[at],a[at+1]];at+=2;p}).collect();
    let knots=a[at..at+nk].to_vec();at+=nk;let weights=a[at..at+nw].to_vec();at+=nw;
    let first=(a[12]!=0.).then_some([a[at],a[at+1]]);
    let end=(a[13]!=0.).then_some([a[at+2],a[at+3]]);let target=[a[at+4],a[at+5]];
    assert_eq!(at+6,a.len());
    let mut result=match ctor {
        0=>NurbsCurve::new(degree,points,knots,Some(weights)),
        1=>NurbsCurve::new_strict(degree,points,knots,weights),
        2=>NurbsCurve::interpolate(&points,first,end,spacing),
        3=>NurbsCurve::interpolate_periodic(&points,spacing),
        4=>NurbsCurve::fit_polyline(&points,amount),
        _=>panic!("constructor"),
    };
    if ctor!=4 {result=result.and_then(|mut c|match tr {
        0|4=>Some(c),1=>Some(c.reversed()),2=>c.elevated(amount as usize),
        3=>{c.insert_knot(amount);Some(c)},5=>c.trimmed(amount,last),_=>panic!("transform"),
    });}
    flag(result.is_some());
    if let Some(c)=result {
        if tr==4 {let split=c.split_at(amount);flag(split.is_some());
            if let Some((left,right))=split {curve(&left,u,t,target,pieces,flags);curve(&right,u,t,target,pieces,flags);}
        } else {curve(&c,u,t,target,pieces,flags);}
    }
}
