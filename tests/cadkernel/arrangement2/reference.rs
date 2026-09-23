// SPDX-License-Identifier: MPL-2.0
// Input/output adapter only: the unchanged pinned module performs all geometry.
use geom2d::{arrangement::*, curve::Line, Tolerance};
fn main() {
    let args:Vec<String>=std::env::args().skip(1).collect();
    let mode:usize=args[0].parse().unwrap();
    let tolerance=Tolerance::new(args[1].parse().unwrap());
    let count:usize=args[2].parse().unwrap();
    let tagged:Vec<TaggedLine>=(0..count).map(|i| {
        let at=3+5*i;
        TaggedLine{line:Line{start:[args[at].parse().unwrap(),args[at+1].parse().unwrap()],
            end:[args[at+2].parse().unwrap(),args[at+3].parse().unwrap()]},tag:args[at+4].parse().unwrap()}
    }).collect();
    let lines:Vec<Line>=tagged.iter().map(|e|e.line).collect();
    let starts:Vec<_>=lines.iter().map(|e|e.start).collect();
    println!("a {:.17e}",signed_area(&starts));
    if mode==1 {
        let crossing=segment_crossing(lines[0],lines[1],tolerance);
        let (kind,a,b)=match crossing {
            SegmentCrossing::None=>(0,[0.,0.],[0.,0.]),
            SegmentCrossing::Point{a,b}=>(1,[a,a],[b,b]),
            SegmentCrossing::Overlap{a,b}=>(2,a,b),
        };
        println!("c {} {:.17e} {:.17e} {:.17e} {:.17e} {}",kind,a[0],a[1],b[0],b[1],usize::from(crossing==crossing.clone()));
    }
    if mode==0 {
        let faces=bounded_faces(&lines,tolerance);
        println!("f {}",faces.len());
        for ring in faces {
            println!("r {} {:.17e}",ring.len(),signed_area(&ring));
            for point in ring {println!("p {:.17e} {:.17e}",point[0],point[1]);}
        }
        let faces=bounded_face_edges(&tagged,tolerance);
        println!("t {}",faces.len());
        for ring in faces {
            let points:Vec<_>=ring.iter().map(|e|e.line.start).collect();
            println!("r {} {:.17e}",ring.len(),signed_area(&points));
            for edge in ring {println!("e {:.17e} {:.17e} {:.17e} {:.17e} {}",edge.line.start[0],edge.line.start[1],edge.line.end[0],edge.line.end[1],edge.tag);}
        }
    }
}
