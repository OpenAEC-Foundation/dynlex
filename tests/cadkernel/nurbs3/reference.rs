// SPDX-License-Identifier: MPL-2.0
// Driver includes unmodified modules from the supplied pinned source checkout.
use space::nurbs::{NurbsCurve3, NurbsSurface3};
use space::spline::Parameterization;
fn emit(x: f64) { println!("{x:.17e}"); }
fn flag(x: bool) { emit(if x {1.0} else {0.0}); }
fn point(p: [f64; 3]) { for x in p { emit(x); } }
fn curve(c: &NurbsCurve3) {
    emit(c.degree() as f64);
    emit(c.control_points().len() as f64);
    emit(c.knots().len() as f64);
    emit(c.weights().len() as f64);
    flag(c.periodicity()); flag(c.is_rational());
    for p in c.control_points() { point(*p); }
    for x in c.knots() { emit(*x); }
    for x in c.weights() { emit(*x); }
    let (a,b)=c.domain(); emit(a); emit(b);
}
fn triple(a: &[f64], i: usize) -> [f64;3] { a[i..i+3].try_into().unwrap() }
fn run_curve(a: &[f64]) {
    let ctor=a[0] as usize;
    let degree=a[1] as usize;
    let n=a[2] as usize;
    let nk=a[3] as usize;
    let nw=a[4] as usize;
    let closed=a[5]!=0.0;
    let transform=a[6] as usize;
    let amount=a[7] as usize;
    let (u,t)=(a[8],a[9]);
    let tess=a[10] as usize;
    let limit=a[11];
    let spacing=match a[12] as usize {0=>Parameterization::Uniform,1=>Parameterization::Centripetal,2=>Parameterization::Chord,_=>panic!("spacing")};
    let evaluate=a[15] as usize;
    let mut cursor=16;
    let points: Vec<_>=(0..n).map(|_| {let p=triple(a,cursor);cursor+=3;p}).collect();
    let knots=a[cursor..cursor+nk].to_vec();cursor+=nk;
    let weights=a[cursor..cursor+nw].to_vec();cursor+=nw;
    let first=(a[13]!=0.0).then(||triple(a,cursor));
    let last=(a[14]!=0.0).then(||triple(a,cursor+3));
    let target=triple(a,cursor+6);
    assert_eq!(cursor+9,a.len());
    let result=match ctor {
        0=>NurbsCurve3::new(degree,points,knots,Some(weights)),
        1=>NurbsCurve3::new_strict(degree,points,knots,weights),
        2=>NurbsCurve3::from_weighted_control_polygon(degree,&points,&weights,closed),
        3=>NurbsCurve3::from_control_polygon(degree,&points,closed),
        4=>NurbsCurve3::interpolate_fit(&points,first,last,spacing),
        5=>NurbsCurve3::interpolate_periodic(&points,spacing),
        _=>panic!("constructor"),
    }.and_then(|c|match transform {
        0=>Some(c),1=>c.reversed(),2=>c.elevated(amount),
        3=>c.without_control_vertex(amount),4=>Some(c.with_periodicity(closed)),
        _=>panic!("transform"),
    });
    flag(result.is_some());
    if let Some(c)=result {
        curve(&c);
        if evaluate>0 {point(c.point_at_knot(u));point(c.point_at(t));}
        if evaluate==1 {
            flag(c.is_closed());point(c.derivative_at_knot(u));
            point(c.tangent_at_knot(u));point(c.tangent_at(t));
            point(c.acceleration_at_knot(u));emit(c.parameter_at(target));
        }
        if tess>0 {
            let p=if tess==1 {c.tessellate_within(limit)} else {c.tessellate_angle(limit)};
            emit(p.len() as f64);for v in p {point(v);}
        }
    }
}
fn run_surface(a: &[f64]) {
    let ctor=a[0] as usize;
    let (ud,vd)=(a[1] as usize,a[2] as usize);
    let (rows,nu,nv,nw)=(a[3] as usize,a[4] as usize,a[5] as usize,a[6] as usize);
    let (uc,vc,rev)=(a[7]!=0.0,a[8]!=0.0,a[9]!=0.0);
    let (u,v,s,t)=(a[10],a[11],a[12],a[13]);
    let (axis,parameter)=(a[14] as usize,a[15]);
    let evaluate=a[16]!=0.0;
    let mut cursor=17;
    let controls=(0..rows).map(|_| {
        let n=a[cursor] as usize;cursor+=1;
        (0..n).map(|_| {let p=triple(a,cursor);cursor+=3;p}).collect()
    }).collect();
    let weights=(0..nw).map(|_| {
        let n=a[cursor] as usize;cursor+=1;
        let r=a[cursor..cursor+n].to_vec();cursor+=n;r
    }).collect();
    let uk=a[cursor..cursor+nu].to_vec();cursor+=nu;
    let vk=a[cursor..cursor+nv].to_vec();cursor+=nv;
    assert_eq!(cursor,a.len());
    let result=match ctor {
        0=>NurbsSurface3::new(ud,vd,controls,uk,vk,Some(weights)),
        1=>NurbsSurface3::new_strict(ud,vd,controls,uk,vk,weights),
        2=>NurbsSurface3::from_control_net(ud,vd,controls,uc,vc),
        _=>panic!("constructor"),
    }.map(|x|x.with_periodicity(uc,vc).with_v_reversed(rev));
    flag(result.is_some());
    if let Some(c)=result {
        let (ud,vd)=c.degrees();emit(ud as f64);emit(vd as f64);
        for b in c.periodicity() {flag(b);}flag(c.v_reversed());
        emit(c.control_points().len() as f64);
        for r in c.control_points() {emit(r.len() as f64);for p in r {point(*p);}}
        emit(c.weights().len() as f64);
        for r in c.weights() {emit(r.len() as f64);for w in r {emit(*w);}}
        let (uk,vk)=c.knots();
        for k in [uk,vk] {emit(k.len() as f64);for x in k {emit(*x);}}
        let ((u0,u1),(v0,v1))=c.domain();for x in [u0,u1,v0,v1] {emit(x);}
        if evaluate {
            point(c.point_at_knot(u,v));point(c.point_at(s,t));
            let tangents=c.tangents_at_knot(u,v);flag(tangents.is_some());
            if let Some((a,b))=tangents {point(a);point(b);}
            for normal in [c.normal_at_knot(u,v),c.normal_at(s,t)] {
                flag(normal.is_some());if let Some(p)=normal {point(p);}
            }
            let iso=c.isocurve(axis,parameter);flag(iso.is_some());
            if let Some(i)=iso {curve(&i);point(i.point_at(s));}
        }
    }
}
fn main() {
    let args: Vec<String>=std::env::args().skip(1).collect();
    let a: Vec<f64>=args[1..].iter().map(|x|x.parse().unwrap()).collect();
    match args[0].as_str() {"curve"=>run_curve(&a),"surface"=>run_surface(&a),_=>panic!("kind")}
}

