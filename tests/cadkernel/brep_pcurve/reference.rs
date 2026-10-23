// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::*;
use cadkernel::brep::pcurve;
use cadkernel::geom2d::{Curve,Line,Circle,NurbsCurve,Tolerance};
use cadkernel::space::{Plane,NurbsCurve3,NurbsSurface3};
fn number(x:f64){println!("{x:0.17e}");}
fn exact(x:usize){println!("d {x}");}
fn flag(x:bool){exact(usize::from(x));}
fn uv(p:[f64;2]){for x in p{number(x);}}
fn point(p:[f64;3]){for x in p{number(x);}}
fn scalars(v:&[f64]){exact(v.len());for x in v{number(*x);}}
fn curve(c:&Curve){
    match c{
        Curve::Line(c)=>{exact(0);uv(c.start);uv(c.end);},
        Curve::Circle(c)=>{exact(1);uv(c.centre);number(c.radius);},
        Curve::Arc(c)=>{exact(2);uv(c.centre);number(c.radius);number(c.start_angle);number(c.end_angle);},
        Curve::Ellipse(c)=>{exact(3);uv(c.ellipse.centre);uv(c.ellipse.major_axis);number(c.ellipse.major_radius);number(c.ellipse.minor_radius);number(c.start_parameter);number(c.end_parameter);},
        Curve::Polyline(c)=>{exact(4);flag(c.closed);exact(c.vertices.len());for v in &c.vertices{uv(v.position);number(v.bulge);}},
        Curve::Nurbs(c)=>{exact(5);exact(c.degree());exact(c.control_points().len());for p in c.control_points(){uv(*p);}scalars(c.knots());scalars(c.weights());},
        Curve::Ray(c)=>{exact(6);uv(c.origin);uv(c.direction);},
        Curve::XLine(c)=>{exact(7);uv(c.base);uv(c.direction);},
    }
    for i in 0..13{uv(c.point_at(i as f64/12.));}
}
fn result(c:Option<Curve>){flag(c.is_some());if let Some(c)=c{curve(&c);}}
fn knots()->Vec<f64>{vec![2.,2.,2.,5.,5.,5.]}
fn spline2()->NurbsCurve{NurbsCurve::new(2,vec![[0.,0.],[1.,2.],[3.,0.]],knots(),Some(vec![1.,0.7,1.])).unwrap()}
fn spline3()->NurbsCurve3{NurbsCurve3::new(2,vec![[0.,0.,0.],[1.,2.,1.],[3.,0.,-0.5]],knots(),Some(vec![1.,0.7,1.])).unwrap()}
fn spline_surface()->NurbsSurface3{
    let p=(0..3).map(|r|(0..3).map(|c|[r as f64,c as f64,if r==1&&c==1{1.}else{0.}]).collect()).collect();
    NurbsSurface3::new(2,2,p,knots(),knots(),None).unwrap()
}
fn triple(a:&[f64],i:usize)->[f64;3]{a[i..i+3].try_into().unwrap()}
fn frame(a:&[f64],i:usize)->Plane{Plane::from_axes(triple(a,i),triple(a,i+3),triple(a,i+6))}
fn one_edge(s:Surface,c:Curve3,first:f64,last:f64)->(Body,FaceKey){
    let mut b=Body::new();let p=Provenance::Synthesized;
    let sk=b.surfaces.insert(s);let ck=b.curves.insert(c);
    let lump=b.lumps.insert(Lump{shells:vec![],provenance:p});
    let sh=b.shells.insert(Shell{faces:vec![],owner:lump,provenance:p});
    let v=b.vertices.insert(Vertex{point:[0.;3],provenance:p});
    let f=b.faces.insert(Face{surface:sk,forward:true,loops:vec![],owner:sh,provenance:p});
    let l=b.loops.insert(Loop{coedges:vec![],owner:f,provenance:p});
    let e=b.edges.insert(Edge{curve:ck,start_parameter:first,end_parameter:last,start:v,end:v,coedges:vec![],provenance:p});
    let u=b.coedges.insert(Coedge{edge:e,forward:true,pcurve:None,owner:l,provenance:p});
    b.faces.get_mut(f).unwrap().loops.push(l);b.loops.get_mut(l).unwrap().coedges.push(u);
    (b,f)
}
fn boundary(b:&Body,f:FaceKey,t:f64){
    let parts=pcurve::verify_parts(b,f,t);flag(parts.is_some());
    if let Some(parts)=parts{exact(parts.len());for(k,c)in parts{exact(k.slot() as usize);curve(&c);}}
    let plain=pcurve::face_boundary(b,f,t);flag(plain.is_some());
    if let Some(plain)=plain{exact(plain.len());for c in plain{curve(&c);}}
}
fn line(start:[f64;2],end:[f64;2])->Curve{Curve::Line(Line{start,end})}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();assert_eq!(a.len(),32);
    let mode=a[0]as usize;let sk=a[1]as usize;let ck=a[2]as usize;let radius=a[3];let minor=a[4];let angle=a[5];
    let cr=a[6];let cm=a[7];let tol=a[8];let first=a[9];let last=a[10];let plane=frame(&a,11);let cp=frame(&a,20);
    let q=[a[29],a[30]];let variant=a[31]as usize;
    let surface=match sk{
        0=>Surface::Plane(plane),1=>Surface::Cylinder(Cylinder{base:plane,radius}),
        2=>Surface::Cone(Cone{base:plane,radius,half_angle:angle}),3=>Surface::Sphere(Sphere{frame:plane,radius}),
        4=>Surface::Torus(Torus{frame:plane,major_radius:radius,minor_radius:minor}),
        5=>Surface::Nurbs(spline_surface().with_periodicity(ck&1!=0,ck&2!=0)),_=>unreachable!()
    };
    for p in pcurve::verify_periods(&surface){flag(p.is_some());if let Some(p)=p{number(p);}}
    if mode==0{
        let c=match ck{
            0=>Curve3::Line(Line3{origin:cp.origin,direction:cp.x_axis}),1=>Curve3::Circle(Circle3{plane:cp,radius:cr}),
            2=>Curve3::Ellipse(Ellipse3{plane:cp,major_radius:cr,minor_radius:cm}),
            3=>Curve3::PlanarSpline{plane:cp,curve:spline2()},4=>Curve3::Nurbs(spline3()),_=>unreachable!()
        };
        result(pcurve::project(&surface,&c,tol));
        let(b,f)=one_edge(surface,c,first,last);boundary(&b,f,tol);
    }else if mode==1{
        let made=match sk{0=>make::cuboid(plane.origin,[radius,minor,cr]),1=>make::cylinder(plane.origin,radius,minor),
            2=>make::cone(plane.origin,radius,minor),3=>make::sphere(plane.origin,radius),_=>unreachable!()};
        flag(made.is_some());
        if let Some(mut b)=made{
            let faces:Vec<_>=b.face_keys().collect();let f=faces[0];let sf=b.faces.get(f).unwrap().surface;
            let l=b.faces.get(f).unwrap().loops[0];let u=b.loops.get(l).unwrap().coedges[0];
            let e=b.coedges.get(u).unwrap().edge;let c=b.edges.get(e).unwrap().curve;
            match variant{
                1=>{b.faces.remove(f);},2=>{b.surfaces.remove(sf);},3=>{b.loops.remove(l);},4=>{b.coedges.remove(u);},
                5=>{b.edges.remove(e);},6=>{b.curves.remove(c);},7=>{b.faces.get_mut(f).unwrap().loops.clear();},
                8=>{let n=b.coedges.get_mut(u).unwrap();n.forward=!n.forward;},
                9=>{let n=b.edges.get_mut(e).unwrap();n.start_parameter=first;n.end_parameter=last;},
                10=>{b.coedges.get_mut(u).unwrap().pcurve=Some(line([17.,19.],[23.,29.]));b.edges.remove(e);},
                11=>{b.loops.get_mut(l).unwrap().coedges.clear();},_=>{}
            }
            exact(faces.len());for f in faces{boundary(&b,f,tol);}
        }
    }else{
        let period=if sk==5{3.}else{std::f64::consts::TAU};
        let mut b=vec![];
        match variant{
            0=>{b.push(line([0.,1.],[period,1.]));b.push(line([period,4.],[0.,4.]));},
            1=>{b.push(line([0.,1.],[period/2.,1.]));b.push(line([period/2.,1.],[period,1.]));b.push(line([period,4.],[0.,4.]));},
            2=>{b.push(line([-0.5,-0.5],[0.5,-0.5]));b.push(line([0.5,-0.5],[0.5,0.5]));b.push(line([0.5,0.5],[-0.5,0.5]));b.push(line([-0.5,0.5],[-0.5,-0.5]));},
            3=>b.push(Curve::Circle(Circle{centre:[0.;2],radius:0.5})),
            4=>{for i in 0..3{b.push(line([0.,i as f64],[period,i as f64]));}},
            5=>{b.push(line([0.,first],[last,first]));b.push(line([last,first+1.],[0.,first+1.]));},
            7=>{b.push(line([0.,0.],[0.,1.]));b.push(line([0.,1.],[0.,0.]));},
            8|11=>{b.push(line([11.,12.],[12.,13.]));b.push(line([0.,0.],[1.,1.]));},
            9=>{b.push(line([11.,12.],[12.,13.]));b.push(Curve::Circle(Circle{centre:[0.;2],radius:1.}));},
            10=>{b.push(Curve::Circle(Circle{centre:[0.;2],radius:1.}));b.push(line([11.,12.],[12.,13.]));},_=>{}
        }
        if mode==3{
            let(mut body,f)=one_edge(surface,Curve3::Line(Line3{origin:[0.;3],direction:[1.,0.,0.]}),0.,1.);
            let mut l=body.faces.get(f).unwrap().loops[0];let u=body.loops.get(l).unwrap().coedges[0];
            let e=body.coedges.get(u).unwrap().edge;body.loops.get_mut(l).unwrap().coedges.clear();
            for(i,c)in b.into_iter().enumerate(){
                if variant==11&&i==1{
                    l=body.loops.insert(Loop{coedges:vec![],owner:f,provenance:Provenance::Synthesized});
                    body.faces.get_mut(f).unwrap().loops.push(l);
                }
                let key=body.coedges.insert(Coedge{edge:e,forward:true,pcurve:Some(c),owner:l,provenance:Provenance::Synthesized});
                body.loops.get_mut(l).unwrap().coedges.push(key);
            }
            boundary(&body,f,tol);
        }else{
            let p=pcurve::verify_band_point(&surface,&b,tol);flag(p.is_some());if let Some(p)=p{point(p);}
            flag(pcurve::verify_contains(&surface,&b,q,Tolerance::new(tol)));
        }
    }
}
