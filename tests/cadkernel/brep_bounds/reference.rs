// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
use cadkernel::space::{Plane,NurbsSurface3};
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn point(p:[f64;3]){for x in p{number(x);}}
fn box3(b:Aabb){point(b.min);point(b.max);}
fn optional(b:Option<Aabb>){flag(b.is_some());if let Some(b)=b{box3(b);}}
fn at(a:&[f64],index:usize)->[f64;3]{[a[index],a[index+1],a[index+2]]}
fn nurbs(value:f64,coordinate:bool)->Surface{
    let mut points=vec![vec![[0.,0.,0.],[1.,2.,3.]],vec![[4.,5.,1.],[6.,7.,2.]]];
    let mut weights=vec![vec![1.,2.],vec![3.,1.]];
    if coordinate{points[0][0][0]=value;}else{weights[0][0]=value;}
    Surface::Nurbs(NurbsSurface3::new(1,1,points,vec![0.,0.,1.,1.],vec![0.,0.,1.,1.],Some(weights)).unwrap())
}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    if a[0]==0.{
        let one=Aabb{min:at(&a,2),max:at(&a,5)};
        let other=Aabb{min:at(&a,8),max:at(&a,11)};
        let p=at(&a,14);
        box3(one);let mut changed=one;changed.absorb(p);box3(changed);
        let mut merged=one;merged.merge(other);box3(merged);
        box3(one.grown(a[1]));point(one.size());point(one.centre());
        flag(one.holds(p));flag(one.overlaps(&other));number(bounds::separation(&one,&other));
        let points:Vec<_>=(0..a[17] as usize).map(|i|at(&a,18+3*i)).collect();
        assert_eq!(a.len(),18+3*points.len());optional(bounds::around_points(&points));
        flag(one==one.clone());return
    }
    let (kind,origin,size,variant,value)=(a[1] as usize,at(&a,2),at(&a,5),a[8] as usize,a[9]);
    let made=match kind{
        0=>make::cuboid(origin,size),1=>make::sphere(origin,size[0]),2=>make::cylinder(origin,size[0],size[1]),
        3=>make::cone(origin,size[0],size[1]),4=>Some(Body::new()),
        5=>{let mut b=Body::new();b.vertices.insert(Vertex{point:origin,provenance:Provenance::Synthesized});Some(b)},_=>panic!("kind"),
    };
    flag(made.is_some());let Some(mut body)=made else{return};
    let faces:Vec<_>=body.face_keys().collect();
    if let Some(&face)=faces.first(){
        let surface=body.faces.get(face).unwrap().surface;
        let coedge=body.face_coedges(face)[0];
        let edge=body.coedges.get(coedge).unwrap().edge;
        let curve=body.edges.get(edge).unwrap().curve;
        let ring=body.faces.get(face).unwrap().loops[0];
        match variant{
            0=>{},1=>{body.faces.remove(face);},2=>{body.surfaces.remove(surface);},
            3=>{body.edges.remove(edge);},4=>{body.curves.remove(curve);},5=>{body.coedges.remove(coedge);},
            6=>{body.loops.remove(ring);},7=>body.faces.get_mut(face).unwrap().loops.clear(),
            8=>{*body.surfaces.get_mut(surface).unwrap()=Surface::Sphere(Sphere{frame:Plane::from_axes(origin,[1.,0.,0.],[0.,value,0.]),radius:2.5});},
            9=>{*body.surfaces.get_mut(surface).unwrap()=Surface::Torus(Torus{frame:Plane::XY,major_radius:value,minor_radius:2.});},
            10|11=>{*body.surfaces.get_mut(surface).unwrap()=nurbs(value,variant==11);},
            12=>{let e=body.edges.get_mut(edge).unwrap();e.start_parameter=value;e.end_parameter=value+3.;},
            13=>{*body.curves.get_mut(curve).unwrap()=Curve3::Circle(Circle3{plane:Plane::from_axes(origin,[1.,0.,0.],[0.,1.,0.]),radius:value});},
            _=>panic!("variant"),
        }
    }
    exact(faces.len());for face in faces{optional(bounds::face_bounds(&body,face));}
    optional(bounds::body_bounds(&body));number(bounds::operation_tolerance(&[&body]));number(bounds::operation_tolerance(&[]));
    let mut remote=Body::new();remote.vertices.insert(Vertex{point:[value;3],provenance:Provenance::Synthesized});
    number(bounds::operation_tolerance(&[&body,&remote]));
}
