// SPDX-License-Identifier: MPL-2.0
// Test driver linked against the complete, unchanged pinned crate.
use cadkernel::brep::*;
use cadkernel::space::{Plane, NurbsSurface3};

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let scenario: i32 = args[0].parse().unwrap();
    let scalar: f64 = args.get(1).map(|s| s.parse().unwrap()).unwrap_or(0.25);
    let clean = Provenance::Clean(SourceRef::new(7));
    let mut b = Body::new();
    b.provenance = clean;
    let lump = b.lumps.insert(Lump { shells: vec![], provenance: clean });
    b.roots.push(lump);
    let shell = b.shells.insert(Shell { faces: vec![], owner: lump, provenance: clean });
    b.lumps.get_mut(lump).unwrap().shells.push(shell);
    let surface = b.surfaces.insert(Surface::Plane(Plane::XY));
    let face = b.faces.insert(Face { surface, forward: true, loops: vec![], owner: shell, provenance: clean });
    b.shells.get_mut(shell).unwrap().faces.push(face);
    let ring = b.loops.insert(Loop { coedges: vec![], owner: face, provenance: clean });
    b.faces.get_mut(face).unwrap().loops.push(ring);
    let start = b.vertices.insert(Vertex { point: [0.0; 3], provenance: clean });
    let end = b.vertices.insert(Vertex { point: [1.0, 0.0, 0.0], provenance: clean });
    let curve = b.curves.insert(Curve3::Line(Line3 { origin: [0.0; 3], direction: [1.0, 0.0, 0.0] }));
    let edge = b.edges.insert(Edge { curve, start_parameter: 0.0, end_parameter: 1.0, start, end, coedges: vec![], provenance: clean });
    let first = b.coedges.insert(Coedge { edge, forward: true, pcurve: None, owner: ring, provenance: clean });
    let second = b.coedges.insert(Coedge { edge, forward: false, pcurve: None, owner: ring, provenance: clean });
    b.edges.get_mut(edge).unwrap().coedges = vec![first, second];
    b.loops.get_mut(ring).unwrap().coedges = vec![first, second];
    // Tombstones are obtained through public arena operations, never forged.
    let dead_lump = b.lumps.insert(b.lumps.get(lump).unwrap().clone()); b.lumps.remove(dead_lump);
    let dead_shell = b.shells.insert(b.shells.get(shell).unwrap().clone()); b.shells.remove(dead_shell);
    let dead_face = b.faces.insert(b.faces.get(face).unwrap().clone()); b.faces.remove(dead_face);
    let dead_ring = b.loops.insert(b.loops.get(ring).unwrap().clone()); b.loops.remove(dead_ring);
    let dead_edge = b.edges.insert(b.edges.get(edge).unwrap().clone()); b.edges.remove(dead_edge);
    let dead_coedge = b.coedges.insert(b.coedges.get(first).unwrap().clone()); b.coedges.remove(dead_coedge);
    match scenario {
        0 => {},
        1 => b.lumps.get_mut(lump).unwrap().shells.clear(),
        2 => b.lumps.get_mut(lump).unwrap().shells.push(dead_shell),
        3 => b.shells.get_mut(shell).unwrap().faces.clear(),
        4 => b.shells.get_mut(shell).unwrap().faces.push(dead_face),
        5 => b.faces.get_mut(face).unwrap().owner = dead_shell,
        6 => { b.surfaces.remove(surface); },
        7 => b.faces.get_mut(face).unwrap().loops.clear(),
        8 => b.faces.get_mut(face).unwrap().loops.push(dead_ring),
        9 => b.loops.get_mut(ring).unwrap().owner = dead_face,
        10 => b.loops.get_mut(ring).unwrap().coedges.clear(),
        11 => b.loops.get_mut(ring).unwrap().coedges = vec![first],
        12 => { b.loops.get_mut(ring).unwrap().coedges = vec![first]; b.edges.get_mut(edge).unwrap().end = start; },
        13 => b.loops.get_mut(ring).unwrap().coedges.push(dead_coedge),
        14 => b.coedges.get_mut(first).unwrap().owner = dead_ring,
        15 => b.coedges.get_mut(second).unwrap().forward = true,
        16 => { b.curves.remove(curve); },
        17 => { b.vertices.remove(start); },
        18 => b.edges.get_mut(edge).unwrap().coedges.clear(),
        19 => b.edges.get_mut(edge).unwrap().coedges = vec![first, first, first],
        20 => b.edges.get_mut(edge).unwrap().coedges.push(dead_coedge),
        21 => b.coedges.get_mut(first).unwrap().edge = dead_edge,
        22 => b.edges.get_mut(edge).unwrap().coedges = vec![first, first],
        23 => b.edges.get_mut(edge).unwrap().coedges = vec![second, first, first, second],
        24 => b.edges.get_mut(edge).unwrap().coedges = vec![first, second, first, first],
        25 => b.edges.get_mut(edge).unwrap().coedges = vec![first],
        26 => { b.coedges.remove(second); },
        27 => { b.edges.remove(edge); },
        28 => { b.loops.remove(ring); },
        29 => { b.faces.remove(face); },
        30 => { b.shells.remove(shell); },
        31 => { b.lumps.remove(lump); },
        32 => b.shells.get_mut(shell).unwrap().owner = dead_lump,
        33 => b.roots.push(dead_lump),
        34 => b.edges.get_mut(edge).unwrap().start_parameter = 2.0,
        35 => b.vertices.get_mut(end).unwrap().point[0] = scalar,
        36 => { b.edges.get_mut(edge).unwrap().start_parameter = scalar; b.edges.get_mut(edge).unwrap().end_parameter = scalar; },
        37 => { b.surfaces.remove(surface); b.faces.get_mut(face).unwrap().loops.clear(); },
        38 => b.loops.get_mut(ring).unwrap().coedges = vec![dead_coedge],
        39 => { b.coedges.get_mut(first).unwrap().owner = dead_ring; b.coedges.get_mut(second).unwrap().owner = dead_ring; },
        40 => b.loops.get_mut(ring).unwrap().coedges = vec![first, first, first],
        41 => b.faces.get_mut(face).unwrap().loops = vec![dead_ring, ring, ring],
        42 => b.edges.get_mut(edge).unwrap().coedges = vec![dead_coedge, first, second, dead_coedge],
        43 => { b.vertices.remove(start); b.vertices.remove(end); },
        44 => { b.loops.get_mut(ring).unwrap().coedges.clear(); b.loops.get_mut(ring).unwrap().owner = dead_face; },
        45..=54 => {
            let value = match scenario {
                45 | 46 => Surface::Sphere(Sphere { frame: Plane::XY, radius: scalar }),
                47 | 48 => Surface::Cone(Cone { base: Plane::XY, radius: 1.0, half_angle: scalar }),
                49 | 50 => Surface::Torus(Torus { frame: Plane::XY, major_radius: scalar, minor_radius: 1.0 }),
                51 | 52 => Surface::Torus(Torus { frame: Plane::XY, major_radius: 0.0, minor_radius: scalar }),
                53 | 54 => Surface::Cylinder(Cylinder { base: Plane::XY, radius: scalar }),
                _ => unreachable!(),
            };
            *b.surfaces.get_mut(surface).unwrap() = value;
            if scenario % 2 == 0 { b.faces.get_mut(face).unwrap().loops.clear(); }
            else { b.loops.get_mut(ring).unwrap().coedges.clear(); }
        },
        55..=59 => {
            let nurbs = NurbsSurface3::new(1, 1, vec![vec![[0.,0.,0.],[0.,1.,0.]], vec![[1.,0.,0.],[1.,1.,0.]]], vec![], vec![], None).unwrap()
                .with_periodicity(scenario == 56 || scenario >= 58, scenario >= 57);
            *b.surfaces.get_mut(surface).unwrap() = Surface::Nurbs(nurbs);
            if scenario == 59 { b.loops.get_mut(ring).unwrap().coedges.clear(); }
            else { b.faces.get_mut(face).unwrap().loops.clear(); }
        },
        _ => panic!("unknown scenario"),
    }
    println!("scenario {scenario}");
    emit(&b, face, edge, first, second, dead_face, dead_edge, dead_coedge);
    let mut duplicate = b.clone();
    if let Some(vertex) = duplicate.vertices.get_mut(end) { vertex.point[0] = 3.0; }
    if let Some(node) = duplicate.lumps.get_mut(lump) { node.shells.clear(); }
    if let Some(node) = duplicate.shells.get_mut(shell) { node.faces.clear(); }
    if let Some(node) = duplicate.faces.get_mut(face) { node.loops.clear(); }
    if let Some(node) = duplicate.loops.get_mut(ring) { node.coedges.clear(); }
    if let Some(node) = duplicate.edges.get_mut(edge) { node.coedges.clear(); }
    duplicate.roots.clear();
    println!("clone");
    emit(&duplicate, face, edge, first, second, dead_face, dead_edge, dead_coedge);
    println!("original");
    emit(&b, face, edge, first, second, dead_face, dead_edge, dead_coedge);
    b.soil_vertex(start);
    for provenance in [
        b.vertices.get(start).map(|n| n.provenance), b.vertices.get(end).map(|n| n.provenance),
        b.edges.get(edge).map(|n| n.provenance), b.coedges.get(first).map(|n| n.provenance),
        b.coedges.get(second).map(|n| n.provenance), b.loops.get(ring).map(|n| n.provenance),
        b.faces.get(face).map(|n| n.provenance), b.shells.get(shell).map(|n| n.provenance),
        b.lumps.get(lump).map(|n| n.provenance), Some(b.provenance),
    ] { emit_provenance(provenance); }
    b.soil_edge(dead_edge);
    b.soil_vertex(start);
    println!("soil idempotent {}", b.vertices.get(start).map(|n| n.provenance.is_reusable()).unwrap_or(false) as i32);
}

fn emit_provenance(value: Option<Provenance>) {
    match value {
        None => println!("provenance absent"),
        Some(value) => println!("provenance {} {} {}", match value { Provenance::Synthesized => 0, Provenance::Clean(_) => 1, Provenance::Dirty(_) => 2 }, value.is_reusable() as i32, value.source().map(|r| r.index()).unwrap_or(0)),
    }
}
fn emit(b: &Body, face: FaceKey, edge: EdgeKey, first: CoedgeKey, second: CoedgeKey, dead_face: FaceKey, dead_edge: EdgeKey, dead_coedge: CoedgeKey) {
    println!("euler {}", b.euler_characteristic());
    println!("number {:.17e}", b.worst_vertex_gap());
    println!("roots {}", b.roots.len());
    let faces: Vec<_> = b.face_keys().collect();
    println!("faces {}", faces.len()); for key in faces { println!("{key:?}"); }
    let edges: Vec<_> = b.edge_keys().collect();
    println!("edges {}", edges.len()); for key in edges { println!("{key:?}"); }
    for key in [first, second, dead_coedge] {
        match b.partner(key) { Some(other) => println!("partner {other:?}"), None => println!("partner absent") }
        match b.coedge_vertices(key) { Some((a, z)) => println!("vertices {a:?} {z:?}"), None => println!("vertices absent") }
    }
    for key in [face, dead_face] {
        let keys = b.face_coedges(key); println!("face uses {}", keys.len()); for key in keys { println!("{key:?}"); }
    }
    for key in [edge, dead_edge] {
        if let Some((start, end)) = b.edge_endpoints(key) {
            println!("endpoints present"); for v in start.into_iter().chain(end) { println!("number {v:.17e}"); }
        } else { println!("endpoints absent"); }
    }
    let flaws = b.validate(); println!("flaws {}", flaws.len());
    for flaw in flaws {
        match flaw {
            Flaw::DanglingKey(message) => println!("0 {message}"),
            Flaw::BrokenOwnership(message) => println!("1 {message}"),
            Flaw::EmptyLump(key) => println!("2 {key:?}"),
            Flaw::EmptyShell(key) => println!("3 {key:?}"),
            Flaw::UnboundedFace(key) => println!("4 {key:?}"),
            Flaw::DegenerateLoop(key) => println!("5 {key:?}"),
            Flaw::OpenLoop(key) => println!("6 {key:?}"),
            Flaw::UnusedEdge(key) => println!("7 {key:?}"),
            Flaw::NonManifoldEdge(key) => println!("8 {key:?}"),
            Flaw::SameSidedEdge(key) => println!("9 {key:?}"),
        }
    }
}
