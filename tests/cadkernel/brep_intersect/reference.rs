// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{intersect::{surfaces,Meeting},Surface,Curve3,Cylinder,Cone,Sphere,Torus};
use cadkernel::space::{Plane,NurbsSurface3};
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn point(p:[f64;3]){for x in p{number(x);}}
fn frame(p:Plane){point(p.origin);point(p.x_axis);point(p.y_axis);}
fn vector(a:&[f64],i:usize)->[f64;3]{[a[i],a[i+1],a[i+2]]}
fn surface(a:&[f64],i:usize)->Surface{
    let p=Plane::from_axes(vector(a,i+1),vector(a,i+4),vector(a,i+7));
    let (r,e)=(a[i+10],a[i+11]);
    match a[i] as usize{
        0=>Surface::Plane(p),1=>Surface::Cylinder(Cylinder{base:p,radius:r}),
        2=>Surface::Cone(Cone{base:p,radius:r,half_angle:e}),
        3=>Surface::Sphere(Sphere{frame:p,radius:r}),
        4=>Surface::Torus(Torus{frame:p,major_radius:r,minor_radius:e}),
        5=>Surface::Nurbs(NurbsSurface3::new(1,1,vec![vec![[0.,0.,0.],[1.,0.,0.]];2],vec![0.,0.,1.,1.],vec![0.,0.,1.,1.],Some(vec![vec![1.,1.];2])).unwrap()),
        _=>panic!("surface variant"),
    }
}
fn emit(m:Meeting){
    match &m{
        Meeting::None=>exact(0),Meeting::Coincident=>exact(3),Meeting::Unknown=>exact(4),
        Meeting::Points(points)=>{exact(2);exact(points.len());for p in points{point(*p);}},
        Meeting::Curves(curves)=>{
            exact(1);exact(curves.len());
            for c in curves{
                match c{
                    Curve3::Line(l)=>{exact(0);point(l.origin);point(l.direction);},
                    Curve3::Circle(c)=>{exact(1);frame(c.plane);number(c.radius);},
                    Curve3::Ellipse(e)=>{exact(2);frame(e.plane);number(e.major_radius);number(e.minor_radius);},
                    _=>panic!("unexpected analytic output"),
                }
                for i in 0..3{point(c.point_at(i as f64-0.25));}
            }
        },
    }
    exact(usize::from(m==m.clone()));
}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    assert_eq!(a.len(),25);
    let (one,other)=(surface(&a,1),surface(&a,13));
    emit(surfaces(&one,&other,a[0]));emit(surfaces(&other,&one,a[0]));
}
