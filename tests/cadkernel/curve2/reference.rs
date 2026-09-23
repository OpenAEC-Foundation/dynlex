// SPDX-License-Identifier: MPL-2.0
// Test driver only. Geometry is evaluated by unchanged pinned source modules.
use geom2d::{curve::*, polyline::*, nurbs::NurbsCurve, Ellipse};
use std::sync::atomic::{AtomicU64,Ordering};
static SCALE:AtomicU64=AtomicU64::new(0);
fn scale(values:impl IntoIterator<Item=f64>){for x in values{if x.is_finite(){let prior=f64::from_bits(SCALE.load(Ordering::Relaxed));SCALE.store(prior.max(x.abs()).to_bits(),Ordering::Relaxed);}}}
fn number(x:f64){println!("{x:.17e}");}
fn flag(x:bool){number(if x{1.}else{0.});}
fn point(x:[f64;2]){println!("p {:.17e} {:.17e} {:.17e}",f64::from_bits(SCALE.load(Ordering::Relaxed)),x[0],x[1]);}
fn points(x:&[[f64;2]]){number(x.len()as f64);for p in x{point(*p);}}
fn curve_samples(shape:&Curve,x:&[[f64;2]]){if matches!(shape,Curve::Line(_)){number(x.len()as f64);for p in x{println!("e {:.17e} {:.17e}",p[0],p[1]);}}else{points(x);}}
fn arc(x:Option<BulgeArc>,t:f64,angle:f64,sampling:bool){
 flag(x.is_some());if let Some(x)=x{
 scale([x.center[0],x.center[1],x.radius]);
 point(x.center);number(x.radius);number(x.start_angle);number(x.end_angle);number(x.sweep);
 point(x.sample(t));flag(x==x);if sampling{points(&x.tessellate_angle(angle));}
 }}
fn polyline(x:&Polyline){flag(x.closed);number(x.vertices.len()as f64);for v in &x.vertices{point(v.position);number(v.bulge);}}
fn main(){
 let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
 let kind=a[0]as usize;let t=a[1];let target=[a[2],a[3]];let density=a[4];let angle=a[5];let flags=a[6]as usize;
 let n=a[7]as usize;let closed=a[8]!=0.;let degree=a[9]as usize;let nk=a[10]as usize;let nw=a[11]as usize;
 let start=[a[12],a[13]];let end=[a[14],a[15]];let radius=a[16];let first=a[17];let last=a[18];let minor=a[19];let axis=[a[20],a[21]];
 let sampling=flags&1!=0;
 if kind!=4&&kind!=5{scale(start);}
 if kind==0||kind==6||kind==7{scale(end);}
 if (1..=3).contains(&kind){scale([radius]);}
 if kind==3{scale([radius*axis[0],radius*axis[1],minor*axis[0],minor*axis[1]]);}
 if kind==8{arc(BulgeArc::from_bulge(start,end,radius),t,angle,sampling);return;}
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
 number(kind as f64);number(match shape.extent(){Extent::Bounded=>0.,Extent::Forward=>1.,Extent::Infinite=>2.});
 flag(shape.extent().holds(t));flag(shape.is_straight());flag(shape.is_closed());number(shape.segment_count()as f64);
 point(shape.point_at(t));point(shape.tangent_at(t));if flags&2!=0{number(shape.parameter_at(target));}
 let ray=shape.as_ray();flag(ray.is_some());if let Some((o,d))=ray{point(o);point(d);}
 let side=shape.rectangle_side([[0.,1.],[0.,1.]]);flag(side.is_some());if let Some(s)=side{number(s as f64);}
 let copy=shape.clone();let clone=shape.clone();flag(shape==copy);flag(shape==clone);
 match &shape{Curve::Line(x)=>{point(x.direction());number(x.length());},Curve::Arc(x)=>number(x.sweep()),Curve::Ellipse(x)=>{number(x.sweep());number(EllipseArc::full(x.ellipse).sweep());},_=>{}}
 let segments=shape.segments();number(segments.len()as f64);
 for segment in segments{
 number(match segment{Curve::Line(_)=>0.,Curve::Arc(_)=>2.,_=>unreachable!()});
 point(segment.point_at(0.));point(segment.point_at(1.));point(segment.tangent_at(t));
 }
 if kind==4{
 polyline(&poly);for i in 0..=n+1{arc(poly.segment_arc(i),t,angle,false);}
 let ranged=poly.ranged(first,last);flag(ranged.is_some());
 if let Some(r)=ranged{
 polyline(&r.polyline);number(r.segments.len()as f64);for s in &r.segments{
 number(s.source_index as f64);number(s.from);number(s.to);point(s.interpolate(radius,minor));
 }flag(r==r.clone());
 }}
 if sampling{curve_samples(&shape,&shape.tessellate(density));curve_samples(&shape,&shape.tessellate_angle(angle));}
}
