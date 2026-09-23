// SPDX-License-Identifier: MPL-2.0
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let geometry=if a[0]==2.{vec![a[1],a[2],a[3],a[4],(a[3]-a[1])*a[5],(a[4]-a[2])*a[5]]}
        else if a[0]==0.{vec![a[1],a[2],a[3]]}
        else{vec![a[1],a[2],a[3]*a[5],a[3]*a[6],a[4]*a[5],a[4]*a[6]]};
    let scale=geometry.iter().filter(|x|x.is_finite()).fold(0f64,|s,x|s.max(x.abs()));
    if a[0]==2.{
        let p=geom2d::tessellate::lerp([a[1],a[2]],[a[3],a[4]],a[5]);
        println!("p {scale:.17e} {:.17e} {:.17e}",p[0],p[1]);
    }else{
        let points=if a[0]==0.{geom2d::tessellate::arc([a[1],a[2]],a[3],a[7],a[8],a[9],a[10])}else{
            geom2d::tessellate::ellipse_arc(&geom2d::Ellipse{centre:[a[1],a[2]],major_radius:a[3],minor_radius:a[4],major_axis:[a[5],a[6]]},a[7],a[8],a[9],a[10])
        };
        println!("d {}",points.len());
        for p in points{println!("p {scale:.17e} {:.17e} {:.17e}",p[0],p[1]);println!("{:.17e}",p[2]);}
    }
}
