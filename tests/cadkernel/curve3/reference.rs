// SPDX-License-Identifier: MPL-2.0
fn emit(value: f64) { println!("{value:.17e}"); }
fn main() {
    let args: Vec<f64> = std::env::args().skip(1).map(|v| v.parse().unwrap()).collect();
    let point = |index: usize| [args[index],args[index+1],args[index+2]];
    match args[0] as usize {
        0 => {
            let controls: Vec<_> = (0..args[1] as usize).map(|i| point(4+3*i)).collect();
            for value in space::curve::bezier_point(&controls,args[2]) { emit(value); }
            let samples = space::curve::bezier_points(&controls,args[3] as usize);
            emit(samples.len() as f64);
            for p in samples { for value in p { emit(value); } }
        },
        1 => {
            for value in space::curve::curvature_through(point(1),point(4),point(7)) { emit(value); }
            let bisector = space::curve::angle_bisector(point(1),point(4),point(7));
            emit(if bisector.is_some() {1.0} else {0.0});
            if let Some(p) = bisector { for value in p { emit(value); } }
        },
        2 => emit(if space::curve::segments_overlap_collinearly(point(1),point(4),point(7),point(10),args[13]) {1.0} else {0.0}),
        _ => panic!("unknown probe mode")
    }
}
