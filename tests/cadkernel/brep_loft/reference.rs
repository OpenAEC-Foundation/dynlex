// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
use cadkernel::geom2d::{Curve,Line,Arc,Ellipse,EllipseArc,NurbsCurve,Circle,Ray,XLine};
use cadkernel::space::Plane;
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn point(p:[f64;3]){for x in p{number(x);}}
fn plane(p:&Plane){point(p.origin);point(p.x_axis);point(p.y_axis);}
fn provenance(p:Provenance){exact(match p{Provenance::Synthesized=>0,Provenance::Clean(_)=>1,Provenance::Dirty(_)=>2});exact(p.source().map(|s|s.index()).unwrap_or(0) as usize);}
fn keys<T>(values:&[Key<T>]){exact(values.len());for k in values{exact(k.slot() as usize);}}
fn emit(b:&Body){
    provenance(b.provenance);keys(&b.roots);
    exact(b.vertices.len());for(k,n)in b.vertices.iter(){exact(k.slot() as usize);point(n.point);provenance(n.provenance);}
    exact(b.edges.len());for(k,n)in b.edges.iter(){exact(k.slot() as usize);exact(n.curve.slot() as usize);number(n.start_parameter);number(n.end_parameter);exact(n.start.slot() as usize);exact(n.end.slot() as usize);keys(&n.coedges);provenance(n.provenance);}
    exact(b.coedges.len());for(k,n)in b.coedges.iter(){exact(k.slot() as usize);exact(n.edge.slot() as usize);flag(n.forward);flag(n.pcurve.is_some());if let Some(Curve::Line(c))=&n.pcurve {for x in c.start.into_iter().chain(c.end){number(x);}}exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.loops.len());for(k,n)in b.loops.iter(){exact(k.slot() as usize);keys(&n.coedges);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.faces.len());for(k,n)in b.faces.iter(){exact(k.slot() as usize);exact(n.surface.slot() as usize);flag(n.forward);keys(&n.loops);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.shells.len());for(k,n)in b.shells.iter(){exact(k.slot() as usize);keys(&n.faces);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.lumps.len());for(k,n)in b.lumps.iter(){exact(k.slot() as usize);keys(&n.shells);provenance(n.provenance);}
    exact(b.curves.len());for(k,n)in b.curves.iter(){exact(k.slot() as usize);match n{
        Curve3::Line(c)=>{exact(0);point(c.origin);point(c.direction);},
        Curve3::Circle(c)=>{exact(1);plane(&c.plane);number(c.radius);},
        Curve3::Nurbs(c)=>{exact(4);exact(c.degree());exact(c.control_points().len());for p in c.control_points(){point(*p);}scalars(c.knots());scalars(c.weights());},
        _=>panic!("unhandled curve"),
    }}
    exact(b.surfaces.len());for(k,n)in b.surfaces.iter(){exact(k.slot() as usize);match n{
        Surface::Plane(p)=>{exact(0);plane(p);},
        Surface::Cylinder(s)=>{exact(1);plane(&s.base);number(s.radius);},
        Surface::Cone(s)=>{exact(2);plane(&s.base);number(s.radius);number(s.half_angle);},
        Surface::Sphere(s)=>{exact(3);plane(&s.frame);number(s.radius);},
        Surface::Nurbs(s)=>{exact(5);let(u,v)=s.degrees();exact(u);exact(v);exact(s.control_points().len());for row in s.control_points(){exact(row.len());for p in row{point(*p);}}let(u,v)=s.knots();scalars(u);scalars(v);exact(s.weights().len());for row in s.weights(){scalars(row);}},
        _=>panic!("unhandled surface"),
    }}
    println!("e {}",b.euler_characteristic());exact(b.validate().len());
}

fn scalars(a:&[f64]){exact(a.len());for x in a{number(*x);}}
struct Reader {a:Vec<f64>,at:usize}
impl Reader {
 fn next(&mut self)->f64{let x=self.a[self.at];self.at+=1;x}
 fn integer(&mut self)->usize{self.next() as usize}
 fn point2(&mut self)->[f64;2]{[self.next(),self.next()]}
 fn point3(&mut self)->[f64;3]{[self.next(),self.next(),self.next()]}
 fn numbers(&mut self)->Vec<f64>{let count=self.integer();(0..count).map(|_|self.next()).collect()}
 fn curve(&mut self)->Curve{match self.integer(){
 0=>Curve::Line(Line{start:self.point2(),end:self.point2()}),
 1=>Curve::Arc(Arc{centre:self.point2(),radius:self.next(),start_angle:self.next(),end_angle:self.next()}),
 2=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre:self.point2(),major_radius:self.next(),minor_radius:self.next(),major_axis:self.point2()},start_parameter:self.next(),end_parameter:self.next()}),
 3=>{let degree=self.integer();let count=self.integer();let points=(0..count).map(|_|self.point2()).collect();let knots=self.numbers();let weights=self.numbers();Curve::Nurbs(NurbsCurve::new(degree,points,knots,Some(weights)).unwrap())},
 4=>Curve::Circle(Circle{centre:self.point2(),radius:self.next()}),
 6=>Curve::Ray(Ray{origin:self.point2(),direction:self.point2()}),
 7=>Curve::XLine(XLine{base:self.point2(),direction:self.point2()}),
 _=>panic!("kind"),}}
}
fn main(){
 let mut r=Reader{a:std::env::args().skip(1).map(|s|s.parse().unwrap()).collect(),at:0};
 let mode=r.integer();let ordered=mode!=0;let count=r.integer();
 let sections:Vec<_>=(0..count).map(|_|{let plane=Plane::from_axes(r.point3(),r.point3(),r.point3());let count=r.integer();(plane,(0..count).map(|_|r.curve()).collect::<Vec<_>>())}).collect();
 assert_eq!(r.at,r.a.len());
 if mode<2{for(_,p)in &sections{for senses in [brep::sweep::export_profile_senses(p),brep::sweep::export_path_senses(p)]{flag(senses.is_some());if let Some(s)=senses{for b in s{flag(b);}}}}}
 let made=if ordered{brep::loft::polygon_loft_ordered(&sections)}else{brep::loft::loft(&sections)};
 flag(made.is_some());if let Some(body)=made{emit(&body);}
}
