// SPDX-License-Identifier: MPL-2.0
// Driver for unmodified pinned endpoint_join.rs, included by verify.py.
use space::endpoint_join::*;
fn joint(value: Option<[f64; 3]>) {
    println!("{}", i32::from(value.is_some()));
    if let Some(point) = value { for x in point { println!("{x:.17e}"); } }
}
fn main() {
    let data: Vec<f64> = std::env::args().skip(1).map(|s| s.parse().unwrap()).collect();
    let mode = data[0] as usize;
    let (fuzz, connector) = (data[1], data[2]);
    let kind = match data[3] as usize { 0 => JoinType::Extend, 1 => JoinType::Add, 2 => JoinType::Both, _ => panic!("invalid kind") };
    let (n, m) = (data[4] as usize, data[5] as usize);
    if mode == 1 {
        let points: Vec<[f64; 2]> = data[6..].chunks_exact(2).map(|p| [p[0], p[1]]).collect();
        assert_eq!(points.len(), n);
        let distance = planar_connector_distance(&points, fuzz);
        println!("{}", i32::from(distance.is_some()));
        if let Some(d) = distance { println!("{d:.17e}"); }
    } else {
        let points: Vec<[[f64; 3]; 2]> = data[6..].chunks_exact(6).map(|p| [[p[0],p[1],p[2]],[p[3],p[4],p[5]]]).collect();
        assert_eq!(points.len(), n + m);
        let (a,b) = points.split_at(n);
        match mode {
            0 => joint(extend_line_ends(a[0], b[0], fuzz)),
            2 => {
                let result = join_line_ends(a[0], b[0], fuzz, connector, kind);
                println!("{}", i32::from(result.is_some()));
                if let Some(value) = result { joint(value); }
            },
            3 => {
                let result = closest_line_end_join(a, b, fuzz, connector, kind);
                println!("{}", i32::from(result.is_some()));
                if let Some((i,j,value)) = result { println!("{i}\n{j}"); joint(value); }
            },
            _ => panic!("invalid mode"),
        }
    }
}
