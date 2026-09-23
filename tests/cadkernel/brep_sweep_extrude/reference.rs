// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::geom2d::{Curve as Curve2,Line,Arc,Circle,Ellipse,EllipseArc,Polyline,Vertex as Vertex2,NurbsCurve,Ray,Xline};
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let mode=a[0] as usize;
    let p3=|i|[a[i],a[i+1],a[i+2]];let p2=|i|[a[i],a[i+1]];
    let frame=Plane::from_axes(p3(1),p3(4),p3(7));let direction=p3(10);
    let count=a[13] as usize;let mut cursor=14;let mut profile=Vec::new();let mut valid=true;
    for _ in 0..count{
        let kind=a[cursor] as usize;cursor+=1;
        let piece=match kind{
            0|6|7=>{let start=p2(cursor);let end=p2(cursor+2);cursor+=4;match kind{0=>Curve2::Line(Line{start,end}),6=>Curve2::Ray(Ray{origin:start,direction:end}),_=>Curve2::Xline(Xline{base:start,direction:end})}},
            1=>{let c=Curve2::Circle(Circle{centre:p2(cursor),radius:a[cursor+2]});cursor+=3;c},
            2=>{let c=Curve2::Arc(Arc{centre:p2(cursor),radius:a[cursor+2],start_angle:a[cursor+3],end_angle:a[cursor+4]});cursor+=5;c},
            3=>{let ellipse=Ellipse{centre:p2(cursor),major_radius:a[cursor+2],minor_radius:a[cursor+3],major_axis:p2(cursor+4)};let c=Curve2::Ellipse(EllipseArc{ellipse,start_parameter:a[cursor+6],end_parameter:a[cursor+7]});cursor+=8;c},
            4|5=>{
                let degree=a[cursor] as usize;let n=a[cursor+1] as usize;cursor+=2;
                let mut points=Vec::new();let mut weights=Vec::new();let mut vertices=Vec::new();
                for _ in 0..n{let point=p2(cursor);let weight=a[cursor+2];points.push(point);weights.push(weight);vertices.push(Vertex2{point,bulge:weight});cursor+=3;}
                let n=a[cursor] as usize;cursor+=1;let knots=a[cursor..cursor+n].to_vec();cursor+=n;
                if kind==4{Curve2::Polyline(Polyline{vertices,closed:degree!=0})}
                else{match NurbsCurve::new(degree,points,knots,Some(weights)){Some(c)=>Curve2::Nurbs(c),None=>{valid=false;Curve2::Line(Line{start:[0.;2],end:[0.;2]})}}}
            },
            _=>panic!("kind"),
        };profile.push(piece);
    }
    assert_eq!(cursor,a.len());
    let result=if !valid{None}else if mode==0{sweep::extrude(frame,&profile,direction)}else{sweep::extrude_surface(frame,&profile,direction)};
    flag(result.is_some());if let Some(body)=result{emit(&body);}
}
