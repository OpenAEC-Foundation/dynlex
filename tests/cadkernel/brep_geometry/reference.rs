// SPDX-License-Identifier: MPL-2.0
use brep::geometry::*;
use space::{NurbsCurve3, NurbsSurface3, Plane};
use geom2d::NurbsCurve;
fn emit(x: f64) { println!("{x:.17e}"); }
fn flag(x: bool) { emit(if x {1.0} else {0.0}); }
fn triple(a: &[f64], i: usize) -> [f64;3] { a[i..i+3].try_into().unwrap() }
fn point(p: [f64;3]) { for x in p {emit(x);} }
fn knots() -> Vec<f64> { vec![2.,2.,2.,5.,5.,5.] }
fn curve2() -> NurbsCurve {
    NurbsCurve::new(2, vec![[0.,0.],[1.,2.],[3.,0.]],knots(),Some(vec![1.,0.7,1.])).unwrap()
}
fn curve3() -> NurbsCurve3 {
    NurbsCurve3::new(2, vec![[0.,0.,0.],[1.,2.,1.],[3.,0.,-0.5]],knots(),Some(vec![1.,0.7,1.])).unwrap()
}
fn surface() -> NurbsSurface3 {
    let points=(0..3).map(|r|(0..3).map(|c|[r as f64,c as f64,if r==1&&c==1 {1.}else{0.}]).collect()).collect();
    NurbsSurface3::new(2,2,points,knots(),knots(),None).unwrap()
}
fn main() {
    let a:Vec<f64>=std::env::args().skip(1).map(|x|x.parse().unwrap()).collect();
    assert_eq!(a.len(),23);
    let plane=Plane::from_axes(triple(&a,2),triple(&a,5),triple(&a,8));
    let radius=a[11]; let minor=a[12]; let angle=a[13]; let u=a[14]; let v=a[15];
    let p=triple(&a,16); let direction=triple(&a,19);
    if a[0]==0. {
        let s=match a[1] as usize {
            0=>Surface::Plane(plane),
            1=>Surface::Cylinder(Cylinder{base:plane,radius}),
            2=>Surface::Cone(Cone{base:plane,radius,half_angle:angle}),
            3=>Surface::Sphere(Sphere{frame:plane,radius}),
            4=>Surface::Torus(Torus{frame:plane,major_radius:radius,minor_radius:minor}),
            5=>Surface::Nurbs(surface()),
            _=>panic!("invalid surface kind"),
        };
        point(s.point_at(u,v));
        let t=s.tangents_at(u,v); flag(t.is_some());
        if let Some((x,y))=t {point(x);point(y);}
        let n=s.normal_at(u,v); flag(n.is_some()); if let Some(x)=n {point(x);}
        let uv=s.parameters_at(p); flag(uv.is_some()); if let Some((x,y))=uv {emit(x);emit(y);}
        let h=s.ray_hits(p,direction); flag(h.is_some());
        if let Some(values)=h {emit(values.len()as f64);for x in values {emit(x);}}
        emit(s.distance_to(p)); flag(s.contains(p,a[22]));
        let f=s.frame(); flag(f.is_some());
        if let Some(f)=f {point(f.origin);point(f.x_axis);point(f.y_axis);}
        flag(s.clone()==s);
    } else {
        let c=match a[1] as usize {
            0=>Curve3::Line(Line3{origin:plane.origin,direction:plane.x_axis}),
            1=>Curve3::Circle(Circle3{plane,radius}),
            2=>Curve3::Ellipse(Ellipse3{plane,major_radius:radius,minor_radius:minor}),
            3=>Curve3::PlanarSpline{plane,curve:curve2()},
            4=>Curve3::Nurbs(curve3()),
            _=>panic!("invalid curve kind"),
        };
        point(c.point_at(u)); point(c.tangent_at(u));emit(c.parameter_at(p));flag(c.clone()==c);
    }
}
