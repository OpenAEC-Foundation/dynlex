// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn point(p:[f64;3]){for x in p{number(x);}}
fn provenance(p:Provenance){exact(match p{Provenance::Synthesized=>0,Provenance::Clean(_)=>1,Provenance::Dirty(_)=>2});exact(p.source().map(|s|s.index()).unwrap_or(0) as usize);}
fn keys<T>(values:&[Key<T>]){exact(values.len());for k in values{exact(k.slot() as usize);}}
fn emit(b:&Body){
    provenance(b.provenance);keys(&b.roots);
    exact(b.vertices.len());for(k,n)in b.vertices.iter(){exact(k.slot() as usize);point(n.point);provenance(n.provenance);}
    let mut edges:Vec<_>=b.edges.iter().collect();
    edges.sort_by_key(|(_,e)|(e.start.slot(),e.end.slot()));
    exact(edges.len());exact(b.curves.len());
    for(_,e)in edges{
        exact(e.start.slot() as usize);exact(e.end.slot() as usize);number(e.start_parameter);number(e.end_parameter);keys(&e.coedges);provenance(e.provenance);
        match b.curves.get(e.curve).unwrap(){Curve3::Line(c)=>{exact(0);point(c.origin);point(c.direction);},_=>panic!("nonlinear facet")}
    }
    exact(b.coedges.len());for(k,n)in b.coedges.iter(){let e=b.edges.get(n.edge).unwrap();exact(k.slot() as usize);exact(e.start.slot() as usize);exact(e.end.slot() as usize);flag(n.forward);flag(n.pcurve.is_some());exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.loops.len());for(k,n)in b.loops.iter(){exact(k.slot() as usize);keys(&n.coedges);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.faces.len());for(k,n)in b.faces.iter(){exact(k.slot() as usize);exact(n.surface.slot() as usize);flag(n.forward);keys(&n.loops);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.shells.len());for(k,n)in b.shells.iter(){exact(k.slot() as usize);keys(&n.faces);exact(n.owner.slot() as usize);provenance(n.provenance);}
    exact(b.lumps.len());for(k,n)in b.lumps.iter(){exact(k.slot() as usize);keys(&n.shells);provenance(n.provenance);}
    exact(b.surfaces.len());for(k,n)in b.surfaces.iter(){exact(k.slot() as usize);match n{Surface::Plane(p)=>{exact(0);point(p.origin);point(p.x_axis);point(p.y_axis);},_=>panic!("nonplanar facet")}}
    println!("e {}",b.euler_characteristic());exact(b.validate().len());number(b.worst_vertex_gap());
}
fn main(){
    let values:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let mut a=values.into_iter();
    let count=a.next().unwrap() as usize;
    let points:Vec<_>=(0..count).map(|_|[a.next().unwrap(),a.next().unwrap(),a.next().unwrap()]).collect();
    let count=a.next().unwrap() as usize;
    let faces:Vec<Vec<usize>>=(0..count).map(|_|{let count=a.next().unwrap() as usize;(0..count).map(|_|{let x=a.next().unwrap();if x<0.{usize::MAX}else{x as usize}}).collect()}).collect();
    assert!(a.next().is_none());
    let made=make::faceted_solid(&points,&faces);
    flag(made.is_some());if let Some(body)=made{emit(&body);}
}
