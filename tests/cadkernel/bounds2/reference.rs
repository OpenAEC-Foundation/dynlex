// Test adapter; all bounds are evaluated by unchanged pinned Rust source.
use geom2d::{curve::*, polyline::*, nurbs::NurbsCurve, Ellipse};
fn main() {
    let a:Vec<f64>=std::env::args().skip(1).map(|s|s.parse().unwrap()).collect();
    let mut offset=1;
    let mut curves=Vec::new();
    for _ in 0..a[0] as usize {
        let kind=a[offset] as usize;
        let centre=[a[offset+1],a[offset+2]];
        let end=[a[offset+3],a[offset+4]];
        let radius=a[offset+5]; let minor=a[offset+6];
        let axis=[a[offset+7],a[offset+8]];
        let first=a[offset+9]; let last=a[offset+10];
        let closed=a[offset+11]!=0.; let count=a[offset+12] as usize;
        offset+=13;
        let vertices=(0..count).map(|i|PolylineVertex::curved([a[offset+3*i],a[offset+3*i+1]],a[offset+3*i+2])).collect();
        offset+=3*count;
        curves.push(match kind {
            0=>Curve::Line(Line{start:centre,end}),
            1=>Curve::Circle(Circle{centre,radius}),
            2=>Curve::Arc(Arc{centre,radius,start_angle:first,end_angle:last}),
            3=>Curve::Ellipse(EllipseArc{ellipse:Ellipse{centre,major_radius:radius,minor_radius:minor,major_axis:axis},start_parameter:first,end_parameter:last}),
            4=>Curve::Polyline(Polyline{vertices,closed}),
            5=>Curve::Nurbs(NurbsCurve::new(1,vec![[0.,0.],[1.,1.]],vec![0.,0.,1.,1.],None).unwrap()),
            6=>Curve::Ray(Ray{origin:centre,direction:end}),
            7=>Curve::XLine(XLine{base:centre,direction:end}),
            _=>unreachable!(),
        });
    }
    match geom2d::bounds::analytic_curve_bounds(&curves) {
        None=>println!("0"),
        Some((low,high))=>{println!("1");for x in low.into_iter().chain(high){println!("{x:.17e}");}}
    }
}
