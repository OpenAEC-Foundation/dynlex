// SPDX-License-Identifier: MPL-2.0
use geom2d::{Curve,Line,Circle,Arc,Ellipse,EllipseArc,Ray,XLine,NurbsCurve,Polyline,PolylineVertex};
use space::{Plane,Vec3};
use brep::nurbs_builder::{RationalCurve2,RationalCurve3};
fn number(x:f64){println!("{x:.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn point<const N:usize>(p:[f64;N],scale:f64){print!("p {scale:.17e}");for x in p{print!(" {x:.17e}");}println!();}
fn scalars(values:&[f64]){exact(values.len());for &v in values{number(v);}}
fn magnitude<const N:usize>(points:&[[f64;N]])->f64{points.iter().flatten().filter(|x|x.is_finite()).fold(0.0f64,|a,x|a.max(x.abs()))}
fn emit2(c:&RationalCurve2){
    exact(c.degree);exact(c.points.len());let scale=magnitude(&c.points);
    for &p in &c.points{point(p,scale);}scalars(&c.knots);scalars(&c.weights);
}
fn emit3(c:&RationalCurve3){
    exact(c.degree);exact(c.points.len());let scale=magnitude(&c.points);
    for &p in &c.points{point(p,scale);}scalars(&c.knots);scalars(&c.weights);
}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let (mode,sweep,offset,mutation,delta)=(a[0] as usize,a[1],Vec3::from([a[2],a[3],a[4]]),a[5] as usize,a[6]);
    let b=&a[7..];
    let kind=b[0] as usize;
    let plane=Plane::from_axes([b[1],b[2],b[3]],[b[4],b[5],b[6]],[b[7],b[8],b[9]]);
    let (start,end,radius,first,last,minor,axis)=([b[10],b[11]],[b[12],b[13]],b[14],b[15],b[16],b[17],[b[18],b[19]]);
    let (n,closed,degree,nk,nw)=(b[20] as usize,b[21]!=0.,b[22] as usize,b[23] as usize,b[24] as usize);
    let mut cursor=32;
    let vertices:Vec<_>=(0..n).map(|i|PolylineVertex::curved([a[cursor+3*i],a[cursor+3*i+1]],a[cursor+3*i+2])).collect();
    cursor+=3*n;
    let knots=a[cursor..cursor+nk].to_vec();cursor+=nk;
    let weights=a[cursor..cursor+nw].to_vec();cursor+=nw;
    assert_eq!(cursor,a.len());
    let made=if mode==1{RationalCurve2::unit_arc(sweep)}else if mode==2{
        Some(RationalCurve2{degree,points:vertices.iter().map(|v|v.position).collect(),knots,weights})
    }else{
        let shape=match kind{
            0=>Curve::Line(Line{start,end}),1=>Curve::Circle(Circle{centre:start,radius}),
            2=>Curve::Arc(Arc{centre:start,radius,start_angle:first,end_angle:last}),
            3=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre:start,major_radius:radius,minor_radius:minor,major_axis:axis},start_parameter:first,end_parameter:last}),
            4=>Curve::Polyline(Polyline{vertices,closed}),
            5=>Curve::Nurbs(NurbsCurve::new(degree,vertices.iter().map(|v|v.position).collect(),knots,Some(weights)).unwrap()),
            6=>Curve::Ray(Ray{origin:start,direction:end}),7=>Curve::XLine(XLine{base:start,direction:end}),_=>panic!("kind"),
        };RationalCurve2::from_curve(&shape)
    };
    flag(made.is_some());let Some(c)=made else{return};
    emit2(&c);emit2(&c.reversed());
    let curve=c.curve();flag(curve.is_some());if let Some(curve)=curve{
        for t in [0.,0.25,0.5,0.75,1.]{point(curve.point_at(t),magnitude(&c.points));}
    }
    let lifted=c.lifted(&plane);emit3(&lifted);emit3(&lifted.reversed());
    let mut other=lifted.translated(offset);emit3(&other);
    let curve=other.curve();flag(curve.is_some());if let Some(curve)=curve{
        for t in [0.,0.25,0.5,0.75,1.]{point(curve.point_at(t),magnitude(&other.points));}
    }
    match mutation{
        0=>{},1=>other.degree+=1,2=>other.knots.push(delta),3=>{other.points.pop();},
        4=>{other.weights.pop();},5=>{if let Some(x)=other.knots.get_mut(0){*x+=delta;}},
        6=>{if let Some(x)=other.weights.get_mut(0){*x=delta;}},
        7=>{if let Some(p)=other.points.get_mut(0){p[0]=delta;}},_=>panic!("mutation"),
    }
    flag(lifted.compatible_with(&other));
    let surface=lifted.ruled_to(&other);flag(surface.is_some());if let Some(surface)=surface{
        let (u,v)=surface.degrees();exact(u);exact(v);let (u,v)=surface.knots();scalars(u);scalars(v);
        let points=surface.control_points();exact(points.len());let scale=magnitude(&lifted.points).max(magnitude(&other.points));
        for row in points{exact(row.len());for &p in row{point(p,scale);}}
        let weights=surface.weights();exact(weights.len());for row in weights{scalars(row);}
        for (s,t) in [(0.,0.),(0.,1.),(0.5,0.5),(1.,0.),(1.,1.)]{point(surface.point_at(s,t),scale);}
    }
}
