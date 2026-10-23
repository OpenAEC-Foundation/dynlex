// SPDX-License-Identifier: MPL-2.0
// Decode inputs and print records; all geometry comes from the pinned module.
use geom2d::{band::*, polyline::*};
fn record(prefix:&str,p:[[f64;2];2],segment:usize,source:[f64;2],local:[f64;2]){
    println!("{} {:.17e} {:.17e} {:.17e} {:.17e} {} {:.17e} {:.17e} {:.17e} {:.17e}",
        prefix,p[0][0],p[0][1],p[1][0],p[1][1],segment,source[0],source[1],local[0],local[1]);
}
fn main(){
    let args:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let count=args[2] as usize;
    let width_count=args[3] as usize;
    let source=Polyline{closed:args[0]!=0.,vertices:(0..count).map(|i|
        PolylineVertex::curved([args[4+3*i],args[5+3*i]],args[6+3*i])).collect()};
    let at=4+3*count;
    let widths:Vec<[f64;2]>=(0..width_count).map(|i|[args[at+2*i],args[at+2*i+1]]).collect();
    let result=polyline_band_boundary(&source,&widths,args[1]);
    println!("b {:.17e} {} {} {}",result.source_length,result.edges.len(),result.station_pieces.len(),usize::from(result==result.clone()));
    for e in result.edges{record("e",e.points,e.source_segment,e.source_distances,e.segment_distances);}
    for e in result.station_pieces{record("s",e.points,e.source_segment,e.source_distances,e.segment_distances);}
}
