// SPDX-License-Identifier: MPL-2.0
fn measure(shape:&Curve,t:f64,target:[f64;2],_:f64,_:f64,_:usize){
 number(shape.length());number(shape.length_to(t));
 number(shape.parameter_at_distance(target[0]));point(shape.point_at_distance(target[0]));
 point(shape.tangent_at(t));
 match shape {Curve::Circle(x)=>number(x.length()),Curve::Arc(x)=>number(x.length()),Curve::Ellipse(x)=>number(x.length()),_=>{}}
}
