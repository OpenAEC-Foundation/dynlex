// SPDX-License-Identifier: MPL-2.0
include!("emit.rs");
fn main(){
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let origin=[a[1],a[2],a[3]];
    let made=match a[0] as usize{
        0=>make::cuboid(origin,[a[4],a[5],a[6]]),
        1=>make::cylinder(origin,a[4],a[5]),
        2=>make::sphere(origin,a[4]),
        3=>make::cone(origin,a[4],a[5]),
        4=>make::pyramid_frustum(origin,a[4],a[6],a[5],a[7] as usize),
        5=>make::elliptical_cylinder(origin,a[4],a[5],a[6]),
        6=>make::frustum(origin,a[4],a[5],a[6],a[7]),
        7=>make::torus(origin,a[4],a[5]),
        _=>panic!("kind"),
    };
    flag(made.is_some());if let Some(body)=made{emit(&body);}
}
