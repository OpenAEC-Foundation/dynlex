// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
use cadkernel::space::{Plane,NurbsCurve3,NurbsSurface3};
use cadkernel::geom2d::{Curve,Line,Circle,Arc,Ellipse,EllipseArc,Polyline,PolylineVertex,Ray,XLine,NurbsCurve};
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn point<const N:usize>(p:[f64;N]){for x in p{number(x);}}
fn plane(p:&Plane){point(p.origin);point(p.x_axis);point(p.y_axis);}
fn provenance(p:Provenance){exact(match p{Provenance::Synthesized=>0,Provenance::Clean(_)=>1,Provenance::Dirty(_)=>2});exact(p.source().map(|s|s.index()).unwrap_or(0) as usize);}
fn keys<T>(values:&[Key<T>]){exact(values.len());for k in values{exact(k.slot() as usize);}}
fn emit(b:&Body){
    provenance(b.provenance);keys(&b.roots);
    exact(b.vertices.len());for(k,n)in b.vertices.iter(){exact(k.slot() as usize);point(n.point);provenance(n.provenance);}
    exact(b.edges.len());for(k,n)in b.edges.iter(){exact(k.slot() as usize);exact(n.curve.slot() as usize);number(n.start_parameter);number(n.end_parameter);exact(n.start.slot() as usize);exact(n.end.slot() as usize);keys(&n.coedges);provenance(n.provenance);}
    exact(b.coedges.len());for(k,n)in b.coedges.iter(){exact(k.slot() as usize);exact(n.edge.slot() as usize);flag(n.forward);flag(n.pcurve.is_some());if let Some(c)=&n.pcurve{pcurve(c);}exact(n.owner.slot() as usize);provenance(n.provenance);}
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
        Surface::Nurbs(s)=>{exact(5);let(u,v)=s.degrees();exact(u);exact(v);for p in s.periodicity(){flag(p);}flag(s.v_reversed());let(u,v)=s.knots();numbers(u);numbers(v);exact(s.control_points().len());for (row,w) in s.control_points().iter().zip(s.weights()){exact(row.len());for p in row{point(*p);}numbers(w);}},
    }}
    println!("e {}",b.euler_characteristic());exact(b.validate().len());
}

fn numbers(xs:&[f64]){exact(xs.len());for &x in xs{number(x);}}
fn spline2(s:&NurbsCurve){exact(s.degree());numbers(s.knots());numbers(s.weights());exact(s.control_points().len());for &p in s.control_points(){point(p);}}
fn spline3(s:&NurbsCurve3){exact(s.degree());flag(s.periodicity());numbers(s.knots());numbers(s.weights());exact(s.control_points().len());for &p in s.control_points(){point(p);}}
fn pcurve(c:&Curve){match c{
 Curve::Line(c)=>{exact(0);point(c.start);point(c.end);},
 Curve::Circle(c)=>{exact(1);point(c.centre);number(c.radius);},
 Curve::Arc(c)=>{exact(2);point(c.centre);number(c.radius);number(c.start_angle);number(c.end_angle);},
 Curve::Ellipse(c)=>{exact(3);point(c.ellipse.centre);point(c.ellipse.major_axis);number(c.ellipse.major_radius);number(c.ellipse.minor_radius);number(c.start_parameter);number(c.end_parameter);},
 Curve::Polyline(c)=>{exact(4);flag(c.closed);exact(c.vertices.len());for v in &c.vertices{point(v.position);number(v.bulge);}},
 Curve::Nurbs(c)=>{exact(5);spline2(c);},
 Curve::Ray(c)=>{exact(6);point(c.origin);point(c.direction);},
 Curve::XLine(c)=>{exact(7);point(c.base);point(c.direction);},
}}
fn spline2_data()->NurbsCurve{NurbsCurve::new(2,vec![[0.,0.],[1.,3.],[3.,-1.]],vec![0.,0.,0.,1.,1.,1.],Some(vec![1.,0.7,1.3])).unwrap()}
fn spline3_data()->NurbsCurve3{NurbsCurve3::new(2,vec![[0.,0.,0.],[1.,3.,2.],[3.,-1.,1.]],vec![0.,0.,0.,1.,1.,1.],Some(vec![1.,0.7,1.3])).unwrap().with_periodicity(true)}
fn surface_data()->NurbsSurface3{NurbsSurface3::new(1,1,vec![vec![[0.,0.,0.],[1.,2.,3.]],vec![[4.,5.,1.],[6.,7.,2.]]],vec![0.,0.,1.,1.],vec![0.,0.,1.,1.],Some(vec![vec![1.,2.],vec![3.,1.]])).unwrap().with_periodicity(true,true).with_v_reversed(true)}
fn pcurve_data(kind:i32)->Curve{let first=[0.2,0.3];let last=[1.4,2.5];match kind{
 1=>Curve::Circle(Circle{centre:first,radius:2.}),
 2=>Curve::Arc(Arc{centre:first,radius:2.,start_angle:0.2,end_angle:2.7}),
 3=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre:first,major_radius:3.,minor_radius:1.,major_axis:[0.6,0.8]},start_parameter:0.2,end_parameter:2.7}),
 4=>Curve::Polyline(Polyline{vertices:vec![PolylineVertex::curved(first,0.4),PolylineVertex::curved(last,-0.3)],closed:false}),
 5=>Curve::Nurbs(spline2_data()),6=>Curve::Ray(Ray{origin:first,direction:last}),7=>Curve::XLine(XLine{base:first,direction:last}),
 _=>Curve::Line(Line{start:first,end:last}),
}}
fn body_data(kind:i32,pc:i32,first:f64,last:f64)->Body{
 let mut b=match kind{0=>make::cuboid([0.;3],[2.,3.,4.]).unwrap(),1=>make::cylinder([1.,2.,3.],2.,4.).unwrap(),2=>make::sphere([1.,2.,3.],2.).unwrap(),3=>make::cone([1.,2.,3.],2.,4.).unwrap(),_=>Body::default()};
 if kind==4{
 let p=Provenance::Clean(SourceRef::new(17));b.provenance=p;
 let vertex=b.vertices.insert(Vertex{point:[1.,2.,3.],provenance:p});
 let lump=b.lumps.insert(Lump{shells:vec![],provenance:p});let shell=b.shells.insert(Shell{faces:vec![],owner:lump,provenance:p});b.lumps.get_mut(lump).unwrap().shells.push(shell);b.roots.push(lump);
 let frame=Plane::from_axes([1.,2.,3.],[0.6,0.8,0.],[-0.8,0.6,0.]);
 for i in 0..6{
  let surface=match i{0=>Surface::Plane(frame),1=>Surface::Cylinder(Cylinder{base:frame,radius:2.}),2=>Surface::Cone(Cone{base:frame,radius:2.,half_angle:0.4}),3=>Surface::Sphere(Sphere{frame,radius:2.}),4=>Surface::Torus(Torus{frame,major_radius:3.,minor_radius:1.}),_=>Surface::Nurbs(surface_data())};
  let surface=b.surfaces.insert(surface);let face=b.faces.insert(Face{surface,forward:true,loops:vec![],owner:shell,provenance:p});let ring=b.loops.insert(Loop{coedges:vec![],owner:face,provenance:p});b.faces.get_mut(face).unwrap().loops.push(ring);b.shells.get_mut(shell).unwrap().faces.push(face);
  let curve=match i{0=>Curve3::Line(Line3{origin:frame.origin,direction:frame.x_axis}),1=>Curve3::Circle(Circle3{plane:frame,radius:2.}),2=>Curve3::Ellipse(Ellipse3{plane:frame,major_radius:3.,minor_radius:1.}),3=>Curve3::PlanarSpline{plane:frame,curve:spline2_data()},_=>Curve3::Nurbs(spline3_data())};
  let curve=b.curves.insert(curve);let edge=b.edges.insert(Edge{curve,start_parameter:first,end_parameter:last,start:vertex,end:vertex,coedges:vec![],provenance:p});let use_key=b.coedges.insert(Coedge{edge,forward:true,pcurve:None,owner:ring,provenance:p});b.loops.get_mut(ring).unwrap().coedges.push(use_key);b.edges.get_mut(edge).unwrap().coedges.push(use_key);
 }
 }
 if kind==6{let p=Provenance::Synthesized;let curve=b.curves.insert(Curve3::PlanarSpline{plane:Plane::XY,curve:spline2_data()});let a=b.vertices.insert(Vertex{point:[0.;3],provenance:p});let z=b.vertices.insert(Vertex{point:[3.,-1.,0.],provenance:p});b.edges.insert(Edge{curve,start_parameter:first,end_parameter:last,start:a,end:z,coedges:vec![],provenance:p});}
 if pc>=0{for c in b.coedges.values_mut(){c.pcurve=Some(pcurve_data(pc));}}
 b
}
fn damage(b:&mut Body,d:i32,value:f64){
 let Some(f)=b.face_keys().next() else{return};let face=b.faces.get(f).unwrap();let surface=face.surface;let ring=face.loops[0];let use_key=b.loops.get(ring).unwrap().coedges[0];let edge=b.coedges.get(use_key).unwrap().edge;let curve=b.edges.get(edge).unwrap().curve;
 match d{
 1=>{b.faces.remove(f);},2=>{b.surfaces.remove(surface);},3=>{b.edges.remove(edge);},4=>{b.curves.remove(curve);},5=>{b.coedges.remove(use_key);},6=>{b.loops.remove(ring);},7=>b.faces.get_mut(f).unwrap().loops.clear(),8=>b.loops.get_mut(ring).unwrap().coedges.clear(),
 9=>{let absent=b.loops.insert(Loop{coedges:vec![],owner:f,provenance:Provenance::Synthesized});b.loops.remove(absent);b.coedges.get_mut(use_key).unwrap().owner=absent;},
 10=>{let absent=b.faces.insert(Face{surface,forward:true,loops:vec![],owner:b.faces.get(f).unwrap().owner,provenance:Provenance::Synthesized});b.faces.remove(absent);b.loops.get_mut(ring).unwrap().owner=absent;},
 11=>{*b.curves.get_mut(curve).unwrap()=Curve3::Circle(Circle3{plane:Plane::from_axes([0.;3],[1.,0.,0.],[0.,value,0.]),radius:2.});},
 12=>{*b.surfaces.get_mut(surface).unwrap()=Surface::Plane(Plane::from_axes([0.;3],[1.,0.,0.],[0.,value,0.]));},
 13=>{let e=b.edges.get_mut(edge).unwrap();e.start_parameter=value;e.end_parameter=value+1.;},
 14=>{*b.curves.get_mut(curve).unwrap()=Curve3::Circle(Circle3{plane:Plane::XY,radius:value});},
 15=>{b.vertices.remove(b.edges.get(edge).unwrap().start);},16=>{let c=b.coedges.get_mut(use_key).unwrap();c.forward=!c.forward;},17=>{b.loops.get_mut(ring).unwrap().coedges.push(use_key);},_=>{}
 }
}
fn samples(b:&Body,sag:f64){
 let keys:Vec<_>=b.edge_keys().collect();exact(keys.len());for key in keys{
 let samples=place::edge_samples(b,key,sag);flag(samples.is_some());if let Some(samples)=samples{exact(samples.len());for s in samples{number(s.parameter);point(s.position);flag(s==s);}}
 let points=edge_points(b,key,sag);flag(points.is_some());if let Some(points)=points{exact(points.len());for p in points{point(p);}}
 }
 let lines=edge_polylines(b,sag);exact(lines.len());for line in lines{exact(line.len());for p in line{point(p);}}
 let mut copy=b.clone();let curve=copy.curves.insert(Curve3::Line(Line3{origin:[0.;3],direction:[1.,0.,0.]}));let v=copy.vertices.insert(Vertex{point:[0.;3],provenance:Provenance::Synthesized});let missing=copy.edges.insert(Edge{curve,start_parameter:0.,end_parameter:1.,start:v,end:v,coedges:vec![],provenance:Provenance::Synthesized});copy.edges.remove(missing);flag(place::edge_samples(&copy,missing,sag).is_some());
}
fn main(){
 let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();let place=Placement{x_axis:[a[7],a[8],a[9]],y_axis:[a[10],a[11],a[12]],z_axis:[a[13],a[14],a[15]],origin:[a[16],a[17],a[18]]};let scale=place.scale();flag(scale.is_some());if let Some(s)=scale{number(s);}flag(place.reflects());point(place.point([0.2,-0.3,0.7]));point(place.vector([0.2,-0.3,0.7]));flag(place==place);flag(place==Placement::IDENTITY);
 let mut original=body_data(a[0]as i32,a[2]as i32,a[4],a[5]);damage(&mut original,a[1]as i32,a[6]);emit(&original);samples(&original,a[3]);let moved=transform(&original,&place);flag(moved.is_some());if let Some(body)=moved{emit(&body);samples(&body,a[3]);}
}
