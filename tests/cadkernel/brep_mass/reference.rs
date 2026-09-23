// SPDX-License-Identifier: MPL-2.0
use cadkernel::brep::{
    analytic_mass_properties, make, Body, Coedge, Cylinder, Edge, Face, Loop, MassProperties,
    Provenance, Sphere, Surface,
};
use cadkernel::geom2d::{Curve, Line};
use cadkernel::space::Plane;

fn patch_corners(angle: f64, height: f64) -> [[f64; 2]; 4] {
    [[0.0, 0.0], [angle, 0.0], [angle, height], [0.0, height]]
}

fn set_patch(body: &mut Body, face: cadkernel::brep::FaceKey, angle: f64, height: f64) {
    let ring = body.faces.get(face).unwrap().loops[0];
    let uses = body.loops.get(ring).unwrap().coedges.clone();
    let corners = patch_corners(angle, height);
    assert_eq!(uses.len(), 4);
    for (index, key) in uses.into_iter().enumerate() {
        body.coedges.get_mut(key).unwrap().pcurve = Some(Curve::Line(Line {
            start: corners[index],
            end: corners[(index + 1) % 4],
        }));
    }
}

fn cylindrical_sector(origin: [f64; 3], outer: f64, inner: f64, height: f64, angle: f64) -> Body {
    let mut body = make::cylinder(origin, outer, height).unwrap();
    let faces: Vec<_> = body.face_keys().collect();
    let outer_face = *faces
        .iter()
        .find(|key| {
            let surface = body.faces.get(**key).unwrap().surface;
            matches!(body.surfaces.get(surface), Some(Surface::Cylinder(_)))
        })
        .unwrap();
    set_patch(&mut body, outer_face, angle, height);
    let owner = body.faces.get(outer_face).unwrap().owner;
    let surface = body.surfaces.insert(Surface::Cylinder(Cylinder {
        base: Plane::from_axes(origin, [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]),
        radius: inner,
    }));
    let inner_face = body.faces.insert(Face {
        surface,
        forward: false,
        loops: Vec::new(),
        owner,
        provenance: Provenance::Synthesized,
    });
    let ring = body.loops.insert(Loop {
        coedges: Vec::new(),
        owner: inner_face,
        provenance: Provenance::Synthesized,
    });
    body.faces.get_mut(inner_face).unwrap().loops.push(ring);
    let vertex = body.vertices.iter().next().unwrap().0;
    let curve = body.curves.iter().next().unwrap().0;
    let corners = patch_corners(angle, height);
    for index in 0..4 {
        let edge = body.edges.insert(Edge {
            curve,
            start_parameter: 0.0,
            end_parameter: 1.0,
            start: vertex,
            end: vertex,
            coedges: Vec::new(),
            provenance: Provenance::Synthesized,
        });
        let usage = body.coedges.insert(Coedge {
            edge,
            forward: true,
            pcurve: Some(Curve::Line(Line {
                start: corners[index],
                end: corners[(index + 1) % 4],
            })),
            owner: ring,
            provenance: Provenance::Synthesized,
        });
        body.edges.get_mut(edge).unwrap().coedges = vec![usage, usage];
        body.loops.get_mut(ring).unwrap().coedges.push(usage);
    }
    body.shells.get_mut(owner).unwrap().faces.push(inner_face);
    body
}

fn spherical_shell(origin: [f64; 3], outer: f64, inner: f64) -> Body {
    let mut body = make::sphere(origin, outer).unwrap();
    let outer_face = body.face_keys().next().unwrap();
    let owner = body.faces.get(outer_face).unwrap().owner;
    let surface = body.surfaces.insert(Surface::Sphere(Sphere {
        frame: Plane::from_axes(origin, [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]),
        radius: inner,
    }));
    let inner_face = body.faces.insert(Face {
        surface,
        forward: false,
        loops: Vec::new(),
        owner,
        provenance: Provenance::Synthesized,
    });
    body.shells.get_mut(owner).unwrap().faces.push(inner_face);
    body
}

fn emit(properties: Option<MassProperties>) {
    let Some(properties) = properties else {
        println!("0");
        return;
    };
    println!("1");
    let values = [
        vec![properties.volume],
        properties.centroid.to_vec(),
        properties.moment_of_inertia.to_vec(),
        properties.principal_directions.to_vec(),
        properties.principal_moments.to_vec(),
        properties.product_of_inertia.to_vec(),
        properties.radii_of_gyration.to_vec(),
    ]
    .concat();
    for value in values {
        println!("{value:.17e}");
    }
}

fn main() {
    let values: Vec<f64> = std::env::args()
        .skip(1)
        .map(|value| value.parse().expect("number"))
        .collect();
    let kind = values[0] as usize;
    let origin = [values[1], values[2], values[3]];
    let body = match kind {
        0 => make::sphere(origin, values[4]).unwrap(),
        1 => make::cylinder(origin, values[4], values[5]).unwrap(),
        2 => make::cone(origin, values[4], values[5]).unwrap(),
        3 => make::cuboid(origin, [values[4], values[5], values[6]]).unwrap(),
        4 => cylindrical_sector(origin, values[4], values[5], values[6], values[7]),
        5 => spherical_shell(origin, values[4], values[5]),
        _ => panic!("shape kind"),
    };
    let delta = [values[8], values[9], values[10]];
    emit(analytic_mass_properties(&body).map(|properties| properties.translated(delta)));
}
