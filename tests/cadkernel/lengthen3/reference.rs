// SPDX-License-Identifier: MPL-2.0
// Reference driver; algorithms and all sixteen tests are unmodified pinned Rust.
use geom2d::{Curve, Line, Circle, Arc, EllipseArc, Ellipse, Ray, XLine, NurbsCurve};
use geom2d::polyline::{Polyline, PolylineVertex};
use space::{plane::Plane, planar::{PlanarCurve, common_curve_plane}};
fn number(x:f64) { println!("{x:.17e}"); }
fn exact(x:usize) { println!("d {x}"); }
fn flag(x:bool) { exact(usize::from(x)); }
fn point(p:[f64;3]) { for x in p { number(x); } }
fn points(p:Vec<[f64;3]>) { exact(p.len()); for p in p { point(p); } }
fn main() {
    use space::lengthen::*;
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let (mode,kind,amount,point,pick)=(a[0] as usize,a[1] as usize,a[2],[a[3],a[4],a[5]],[a[6],a[7],a[8]]);
    let change=match kind {0=>LengthChange::Delta(amount),1=>LengthChange::Total(amount),2=>LengthChange::Percent(amount),3=>LengthChange::DeltaAngle(amount),4=>LengthChange::TotalAngle(amount),5=>LengthChange::Dynamic(point),_=>panic!("change")};
    let mut cursor=15;
    let mut curves=Vec::new();
    for _ in 0..1 {
        let b=&a[cursor..];
        let kind=b[0] as usize;
        let plane=Plane::from_axes([b[1],b[2],b[3]],[b[4],b[5],b[6]],[b[7],b[8],b[9]]);
        let (start,end,radius,first,last,minor,axis)=([b[10],b[11]],[b[12],b[13]],b[14],b[15],b[16],b[17],[b[18],b[19]]);
        let (n,closed,degree,nk,nw)=(b[20] as usize,b[21]!=0.,b[22] as usize,b[23] as usize,b[24] as usize);
        cursor+=25;
        let vertices:Vec<_>=(0..n).map(|i|PolylineVertex::curved([a[cursor+3*i],a[cursor+3*i+1]],a[cursor+3*i+2])).collect();
        cursor+=3*n;
        let knots=a[cursor..cursor+nk].to_vec();cursor+=nk;
        let weights=a[cursor..cursor+nw].to_vec();cursor+=nw;
        let curve=match kind {
            0=>Curve::Line(Line{start,end}),
            1=>Curve::Circle(Circle{centre:start,radius}),
            2=>Curve::Arc(Arc{centre:start,radius,start_angle:first,end_angle:last}),
            3=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre:start,major_radius:radius,minor_radius:minor,major_axis:axis},start_parameter:first,end_parameter:last}),
            4=>Curve::Polyline(Polyline{vertices,closed}),
            5=>Curve::Nurbs(NurbsCurve::new(degree,vertices.iter().map(|v|v.position).collect(),knots,Some(weights)).unwrap()),
            6=>Curve::Ray(Ray{origin:start,direction:end}),
            7=>Curve::XLine(XLine{base:start,direction:end}),
            _=>panic!("invalid kind"),
        };
        curves.push(PlanarCurve::new(plane,curve));
    }
    assert_eq!(cursor,a.len());
    let curve=&curves[0];
    let before=curve.clone();
    match mode {
        0=>{let result=lengthen_line([a[9],a[10],a[11]],[a[12],a[13],a[14]],pick,change);
            flag(result.is_some());if let Some(p)=result{for p in p{for x in p{number(x);}}}},
        1|2=>{let result=if mode==1{lengthen_arc(curve,pick,change)}else{lengthen_ellipse(curve,pick,change)};
            flag(result.is_some());if let Some((a,b))=result{number(a);number(b);}},
        3=>{let result=lengthen_polyline(curve,pick,change);
            flag(result.is_some());if let Some(vertices)=result{exact(vertices.len());for v in vertices{
                number(v.position[0]);number(v.position[1]);number(v.bulge);exact(v.source);number(v.start_fraction);number(v.end_fraction);
            }}},
        _=>panic!("mode"),
    }
    flag(*curve==before);
}
