// SPDX-License-Identifier: MPL-2.0
// Reference driver; algorithms and all sixteen tests are unmodified pinned Rust.
use geom2d::{Curve, Line, Circle, Arc, EllipseArc, Ellipse, Ray, XLine, NurbsCurve};
use geom2d::polyline::{Polyline, PolylineVertex};
use space::{plane::Plane, planar::{PlanarCurve, common_curve_plane}};
fn number(x:f64) { println!("f {x:.17e}"); }
fn exact(x:usize) { println!("e {x}"); }
fn flag(x:bool) { exact(usize::from(x)); }
fn point(p:[f64;3]) { for x in p { number(x); } }
fn points(p:Vec<[f64;3]>) { exact(p.len()); for p in p { point(p); } }
fn main() {
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let (mode,tol,t,target,density,angle,count)=(a[0] as usize,a[1],a[2],[a[3],a[4],a[5]],a[6],a[7],a[8] as usize);
    let mut cursor=9;
    let mut curves=Vec::new();
    for _ in 0..count {
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
    if mode==0 {
        let result=common_curve_plane(&curves,tol);
        flag(result.is_some());
        if let Some(p)=result { point(p.origin);point(p.x_axis);point(p.y_axis); }
    } else {
        let shape=&curves[0];
        point(shape.point_at(t));
        let back=shape.parameter_at(target);flag(back.is_some());if let Some(x)=back{number(x);}
        let normal=shape.normal();flag(normal.is_some());if let Some(x)=normal{point(x);}
        exact(match shape.extent(){geom2d::Extent::Bounded=>0,geom2d::Extent::Forward=>1,geom2d::Extent::Infinite=>2});
        flag(shape.is_closed());point(shape.tangent_at(t));flag(shape.clone()==*shape);
        if mode==2 { points(shape.tessellate(density));points(shape.tessellate_angle(angle)); }
        if mode==3 {
            number(shape.length());point(shape.point_at_distance(t));
            number(shape.parameter_at_distance(t));points(shape.tessellate_within(tol));
        }
    }
}
