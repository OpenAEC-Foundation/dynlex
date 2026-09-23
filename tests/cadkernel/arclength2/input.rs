// SPDX-License-Identifier: MPL-2.0
// Test driver only. Geometry is evaluated by unchanged pinned source modules.
use geom2d::{curve::*, polyline::*, nurbs::NurbsCurve, Ellipse};
use std::sync::atomic::{AtomicU64,Ordering};
static SCALE:AtomicU64=AtomicU64::new(0);
fn scale(values:impl IntoIterator<Item=f64>){for x in values{if x.is_finite(){let prior=f64::from_bits(SCALE.load(Ordering::Relaxed));SCALE.store(prior.max(x.abs()).to_bits(),Ordering::Relaxed);}}}
fn number(x:f64){println!("{x:.17e}");}
fn flag(x:bool){println!("d {}",if x{1}else{0});}
fn point(x:[f64;2]){println!("p {:.17e} {:.17e} {:.17e}",f64::from_bits(SCALE.load(Ordering::Relaxed)),x[0],x[1]);}
fn points(x:&[[f64;2]]){println!("d {}",x.len());for p in x{point(*p);}}
fn curve_samples(shape:&Curve,x:&[[f64;2]]){if matches!(shape,Curve::Line(_)){println!("d {}",x.len());for p in x{println!("e {:.17e} {:.17e}",p[0],p[1]);}}else{points(x);}}
fn main(){
 let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
 let kind=a[0]as usize;let t=a[1];let target=[a[2],a[3]];let density=a[4];let angle=a[5];let flags=a[6]as usize;
 let n=a[7]as usize;let closed=a[8]!=0.;let degree=a[9]as usize;let nk=a[10]as usize;let nw=a[11]as usize;
 let start=[a[12],a[13]];let end=[a[14],a[15]];let radius=a[16];let first=a[17];let last=a[18];let minor=a[19];let axis=[a[20],a[21]];
 if kind!=4&&kind!=5{scale(start);}
 if kind==0||kind==6||kind==7{scale(end);}
 if (1..=3).contains(&kind){scale([radius]);}
 if kind==3{scale([radius*axis[0],radius*axis[1],minor*axis[0],minor*axis[1]]);}

 let vertices:Vec<_>=(0..n).map(|i|PolylineVertex::curved([a[22+3*i],a[23+3*i]],a[24+3*i])).collect();
 let controls=vertices.iter().map(|v|v.position).collect();
 let poly=Polyline{vertices,closed};
 if kind==4||kind==5{for v in &poly.vertices{scale(v.position);}}
 if kind==4{for i in 0..poly.vertices.len(){if let Some(x)=poly.segment_arc(i){scale([x.center[0],x.center[1],x.radius]);}}}
 let knots=a[22+3*n..22+3*n+nk].to_vec();let weights=a[22+3*n+nk..22+3*n+nk+nw].to_vec();
 let shape=match kind{
 0=>Curve::Line(Line{start,end}),1=>Curve::Circle(Circle{centre:start,radius}),
 2=>Curve::Arc(Arc{centre:start,radius,start_angle:first,end_angle:last}),
 3=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre:start,major_radius:radius,minor_radius:minor,major_axis:axis},start_parameter:first,end_parameter:last}),
 4=>Curve::Polyline(poly.clone()),5=>{let result=NurbsCurve::new(degree,controls,knots,Some(weights));flag(result.is_some());if let Some(c)=result{Curve::Nurbs(c)}else{return;}},
 6=>Curve::Ray(Ray{origin:start,direction:end}),7=>Curve::XLine(XLine{base:start,direction:end}),_=>unreachable!()};
 measure(&shape,t,target,density,angle,flags);
}
