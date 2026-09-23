// SPDX-License-Identifier: MPL-2.0
fn measure(shape:&Curve,_:f64,_:[f64;2],tolerance:f64,angle:f64,flags:usize){
 curve_samples(shape,&shape.tessellate_within(tolerance));
 if flags&1!=0 {curve_samples(shape,&shape.tessellate_angle(angle));}
}
