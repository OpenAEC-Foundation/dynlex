// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
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
    exact(b.coedges.len());for(k,n)in b.coedges.iter(){exact(k.slot() as usize);exact(n.edge.slot() as usize);flag(n.forward);flag(n.pcurve.is_some());if let Some(c)=&n.pcurve{match c{cadkernel::geom2d::Curve::Line(l)=>{exact(0);for x in l.start{number(x);}for x in l.end{number(x);}},_=>panic!("unexpected pcurve")}}exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.loops.len());for(k,n)in b.loops.iter(){exact(k.slot() as usize);keys(&n.coedges);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.faces.len());for(k,n)in b.faces.iter(){exact(k.slot() as usize);exact(n.surface.slot() as usize);flag(n.forward);keys(&n.loops);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.shells.len());for(k,n)in b.shells.iter(){exact(k.slot() as usize);keys(&n.faces);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.lumps.len());for(k,n)in b.lumps.iter(){exact(k.slot() as usize);keys(&n.shells);provenance(n.provenance);}
    exact(b.curves.len());for(k,n)in b.curves.iter(){exact(k.slot() as usize);match n{
        Curve3::Line(c)=>{exact(0);point(c.origin);point(c.direction);},
        Curve3::Circle(c)=>{exact(1);plane(&c.plane);number(c.radius);},
        Curve3::Ellipse(c)=>{exact(2);plane(&c.plane);number(c.major_radius);number(c.minor_radius);},
        Curve3::PlanarSpline{plane:p,curve:c}=>{exact(3);plane(p);spline2(c);},
        Curve3::Nurbs(c)=>{exact(4);spline3(c);},
    }}
    exact(b.surfaces.len());for(k,n)in b.surfaces.iter(){exact(k.slot() as usize);match n{
        Surface::Plane(p)=>{exact(0);plane(p);},
        Surface::Cylinder(s)=>{exact(1);plane(&s.base);number(s.radius);},
        Surface::Cone(s)=>{exact(2);plane(&s.base);number(s.radius);number(s.half_angle);},
        Surface::Sphere(s)=>{exact(3);plane(&s.frame);number(s.radius);},
        Surface::Torus(s)=>{exact(4);plane(&s.frame);number(s.major_radius);number(s.minor_radius);},
        Surface::Nurbs(s)=>{exact(5);surface(s);},
    }}
    println!("e {}",b.euler_characteristic());exact(b.validate().len());
}

fn scalars(values:&[f64]){exact(values.len());for &x in values{number(x);}}
fn spline2(c:&cadkernel::geom2d::NurbsCurve){exact(c.degree());flag(c.is_closed());scalars(c.knots());scalars(c.weights());exact(c.control_points().len());for p in c.control_points(){for &x in p{number(x);}}}
fn spline3(c:&cadkernel::space::NurbsCurve3){exact(c.degree());flag(c.is_closed());scalars(c.knots());scalars(c.weights());exact(c.control_points().len());for &p in c.control_points(){point(p);}}
fn surface(s:&cadkernel::space::NurbsSurface3){
    let(u,v)=s.degrees();exact(u);exact(v);for b in s.periodicity(){flag(b);}flag(s.v_reversed());
    let(u,v)=s.knots();scalars(u);scalars(v);exact(s.weights().len());for row in s.weights(){scalars(row);}
    exact(s.control_points().len());for row in s.control_points(){exact(row.len());for &p in row{point(p);}}
}
