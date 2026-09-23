// Runtime libm diagnostic; no geometry is reimplemented.
extern "C" { #[link_name="sin"] fn c_sin(x:f64)->f64; }
fn main(){let x:f64=std::env::args().nth(1).unwrap().parse().unwrap();println!("{:.17e} {:.17e}",x.sin(),unsafe{c_sin(x)});}
