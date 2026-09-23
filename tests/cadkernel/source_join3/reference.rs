// SPDX-License-Identifier: MPL-2.0
use space::{NurbsCurve3,source_join::*};
fn emit(x:f64){println!("{x:.17e}");}
fn integer(x:usize){println!("d {x}");}
fn flag(x:bool){integer(usize::from(x));}
fn point(p:[f64;3]){for x in p{emit(x);}}
fn triple(a:&[f64],i:usize)->[f64;3]{a[i..i+3].try_into().unwrap()}
fn curve(c:&NurbsCurve3){
    integer(c.degree());integer(c.control_points().len());
    integer(c.knots().len());integer(c.weights().len());
    flag(c.periodicity());flag(c.is_rational());
    for p in c.control_points(){point(*p);}for x in c.knots(){emit(*x);}for x in c.weights(){emit(*x);}
    let(a,b)=c.domain();emit(a);emit(b);
}
fn read_curve(a:&[f64])->Option<NurbsCurve3>{
    let(degree,n,nk,nw)=(a[0] as usize,a[1] as usize,a[2] as usize,a[3] as usize);
    let points=(0..n).map(|i|triple(a,5+3*i)).collect();
    let end=5+3*n;
    NurbsCurve3::new(degree,points,a[end..end+nk].to_vec(),Some(a[end+nk..end+nk+nw].to_vec())).map(|c|c.with_periodicity(a[4]!=0.))
}
fn span(result:Option<[f64;2]>){flag(result.is_some());if let Some([a,b])=result{emit(a);emit(b);}}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    match a[0] as usize{
        0=>{let result=join_collinear_lines([triple(&a,2),triple(&a,5)],[triple(&a,8),triple(&a,11)],a[1]);flag(result.is_some());if let Some([a,b])=result{point(a);point(b);}},
        1=>span(join_counterclockwise_spans([a[2],a[3]],[a[4],a[5]])),
        2=>span(join_cocircular_arcs((triple(&a,2),triple(&a,5),a[8],[a[9],a[10]]),(triple(&a,11),triple(&a,14),a[17],[a[18],a[19]]),a[1])),
        3=>{let result=line_as_nurbs([triple(&a,3),triple(&a,6)],a[2] as usize);flag(result.is_some());if let Some(c)=result{curve(&c);}},
        4=>{
            let source=read_curve(&a[3..]);let other=read_curve(&a[a[2] as usize..]);
            flag(source.is_some());flag(other.is_some());
            if let(Some(source),Some(other))=(source,other){
                let before_source=source.clone();let before_other=other.clone();
                let result=join_nurbs_curves(&source,&other,a[1]);flag(result.is_some());
                if let Some(c)=result{curve(&c);for t in [0.,0.13,0.25,0.5,0.75,0.87,1.]{point(c.point_at(t));}flag(c.control_points().as_ptr()!=source.control_points().as_ptr()&&c.control_points().as_ptr()!=other.control_points().as_ptr());}
                flag(source==before_source);flag(other==before_other);
            }
        },
        _=>panic!("invalid fixture operation")
    }
}
