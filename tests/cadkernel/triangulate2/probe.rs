// SPDX-License-Identifier: MPL-2.0
// Appended inside the unmodified pinned triangulate module for private-helper checks.
fn probe_scalar(value: f64) { println!("{value:.17e}"); }
fn probe_points(points: &[[f64; 2]]) {
    println!("{}", points.len());
    for point in points { probe_scalar(point[0]); probe_scalar(point[1]); }
}
fn probe_triangles(triangles: &[[usize; 3]]) {
    println!("{}", triangles.len());
    for triangle in triangles { for index in triangle { println!("{index}"); } }
}
fn probe_vecs(points: &[Vec2]) { probe_points(&points.iter().copied().map(Vec2::to_array).collect::<Vec<_>>()); }
pub fn run_probe(values: &[f64]) {
    let mode = values[0] as usize;
    let linear = values[1];
    let mut cursor = 3;
    let mut input = Vec::new();
    for _ in 0..values[2] as usize {
        let count = values[cursor] as usize;
        cursor += 1;
        let points = values[cursor..cursor + 2 * count].chunks_exact(2).map(|p| [p[0], p[1]]).collect::<Vec<_>>();
        cursor += 2 * count;
        input.push(points);
    }
    match mode {
        0 | 3 => {
            let (points, triangles) = if mode == 0 { polygon(&input[0], &input[1..]) } else { rings(&input) };
            probe_points(&points); probe_triangles(&triangles);
        }
        1 => match polygon_frame(&input[0], super::Tolerance::new(linear)) {
            None => println!("false"),
            Some(frame) => {
                println!("true");
                for value in [frame.origin[0], frame.origin[1], frame.size[0], frame.size[1]] { probe_scalar(value); }
                probe_points(&frame.points);
            }
        },
        2 => { let depths = nesting_depths(&input); println!("{}", depths.len()); for depth in depths { println!("{depth}"); } }
        4 => {
            let ring = &input[0]; let other = &input[1]; let point = input[2][0];
            probe_scalar(signed_area_arrays(ring)); println!("{}", simple(ring, linear));
            probe_points(&sanitize(ring)); probe_vecs(&orient(ring, true)); probe_vecs(&orient(ring, false));
            match RingBounds::of(ring) {
                None => println!("false"),
                Some(bounds) => { println!("true"); probe_scalar(bounds.min.x); probe_scalar(bounds.min.y); probe_scalar(bounds.max.x); probe_scalar(bounds.max.y); }
            }
            if !ring.is_empty() { println!("{}", rightmost(&ring.iter().copied().map(Vec2::from).collect::<Vec<_>>()).0); }
            println!("{}", ring_contains(ring, point)); println!("{}", point_on_boundary(ring, point));
            println!("{}", ring_strictly_contains(ring, other)); println!("{}", ring_strictly_contains(other, ring));
            println!("{}", ring_boundaries_intersect(ring, other)); println!("{}", ring_boundaries_properly_cross(ring, other));
            if let (Some(a), Some(b)) = (RingBounds::of(ring), RingBounds::of(other)) {
                println!("{}", a.overlaps(b));
                let a = CheckedRing { points: ring.clone(), area: signed_area_arrays(ring).abs(), bounds: a };
                let b = CheckedRing { points: other.clone(), area: signed_area_arrays(other).abs(), bounds: b };
                println!("{}", rings_interact(&a, &b));
            }
        }
        5 => {
            let p = &input[0]; let (a,b,c,d) = (p[0],p[1],p[2],p[3]);
            println!("{}", segments_properly_cross(a,b,c,d)); println!("{}", segments_intersect(a,b,c,d));
            println!("{}", segments_intersect_with_tolerance(a,b,c,d,linear));
            println!("{}", point_on_segment(a,b,c)); println!("{}", point_on_segment(a,b,d));
            println!("{}", inside(a.into(),b.into(),c.into(),d.into()));
        }
        6 => {
            let faces = values[cursor..].chunks_exact(3).map(|p| [p[0] as usize,p[1] as usize,p[2] as usize]).collect::<Vec<_>>();
            println!("{}", triangulation_matches(&input[0],&faces,linear));
        }
        7 => {
            let points = input[0].iter().copied().map(Vec2::from).collect::<Vec<_>>();
            let ring = input[1].iter().map(|p| p[0] as usize).collect::<Vec<_>>();
            for at in 0..ring.len() { println!("{}", is_ear(&points,&ring,at)); probe_scalar(ear_score(&points,&ring,at,input[2][0])); }
            probe_triangles(&clip_ears(&points,ring.clone()));
            let (mut p, mut r) = (points.clone(),ring.clone());
            match bridge_target(&mut p,&mut r,input[2][1].into()) { Some(at) => println!("{at}"), None => println!("-1") }
            probe_vecs(&p); println!("{}",r.len()); for index in r { println!("{index}"); }
            let (mut p, mut r) = (points,ring);
            bridge(&mut p,&mut r,input[3].iter().copied().map(Vec2::from).collect());
            probe_vecs(&p); println!("{}",r.len()); for index in r { println!("{index}"); }
        }
        8 => {
            for pair in &input[0] {
                println!("{}", match pair[0].total_cmp(&pair[1]) { std::cmp::Ordering::Less => -1, std::cmp::Ordering::Equal => 0, std::cmp::Ordering::Greater => 1 });
            }
        }
        9 => {
            let parents = input[0].iter().map(|p| if p[0] < 0.0 { None } else { Some(p[0] as usize) }).collect::<Vec<_>>();
            for index in 0..parents.len() { println!("{}", root_of(index,&parents)); }
        }
        _ => panic!("unknown probe mode"),
    }
}
