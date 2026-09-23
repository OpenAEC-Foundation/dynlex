pub fn run_probe() {
    let mut values = std::env::args().skip(1).map(|value| value.parse::<f64>().unwrap());
    let ring_count = values.next().unwrap() as usize;
    let mut rings = Vec::with_capacity(ring_count);
    for _ in 0..ring_count {
        let count = values.next().unwrap() as usize;
        rings.push((0..count).map(|_| [values.next().unwrap(), values.next().unwrap()]).collect());
    }
    let operation_count = values.next().unwrap() as usize;
    let Some(mut mesh) = ConstrainedMesh::new(&rings) else {
        println!("b 0");
        return;
    };
    println!("b 1");
    for _ in 0..operation_count {
        let kind = values.next().unwrap() as usize;
        let from = [values.next().unwrap(), values.next().unwrap()];
        if kind == 0 {
            match mesh.insert(from) {
                Some(inserted) => println!("i 1 {}", inserted as usize),
                None => println!("i 0 0"),
            }
        } else {
            let to = [values.next().unwrap(), values.next().unwrap()];
            match mesh.constrain(from, to) {
                Some(added) => println!("c 1 {added}"),
                None => println!("c 0 0"),
            }
        }
    }
    let triangles = mesh.triangles();
    println!("n {}", triangles.len());
    for triangle in triangles {
        print!("t");
        for point in triangle.parameters {
            print!(" {:.17e} {:.17e}", point[0], point[1]);
        }
        for constrained in triangle.constraints {
            print!(" {}", constrained as usize);
        }
        println!();
    }
}
