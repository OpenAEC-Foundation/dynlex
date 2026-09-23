// SPDX-License-Identifier: MPL-2.0
use space::NurbsCurve3;
fn integer(x:usize){println!("d {x}");}
fn flag(x:bool){integer(usize::from(x));}
fn number(x:f64){println!("{x:.17e}");}
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let(n,nk,nw)=(a[2] as usize,a[3] as usize,a[4] as usize);
    let points=(0..n).map(|i|a[6+3*i..9+3*i].try_into().unwrap()).collect();
    let end=6+3*n;
    let built=NurbsCurve3::new(a[1] as usize,points,a[end..end+nk].to_vec(),Some(a[end+nk..end+nk+nw].to_vec()));
    flag(built.is_some());
    if let Some(curve)=built{
        let curve=curve.with_periodicity(a[5]!=0.);
        let original=curve.clone();
        let result=curve.to_polyline_precision(a[0] as u8);
        flag(result.is_some());
        if let Some(result)=result{
            number(result.tolerance);integer(result.points.len());
            for p in result.points{for x in p{number(x);}}
        }
        flag(curve==original);
    }
}
