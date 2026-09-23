// SPDX-License-Identifier: MPL-2.0
include!("../brep_make/emit.rs");
use cadkernel::brep::make;
use cadkernel::brep::{
    planar_region, slice_by_plane, slice_by_surface, surface_side, Body, Cone, Cylinder,
    Face, Lump, Provenance, Shell, Snag, Sphere, Surface, Torus,
};
use cadkernel::geom2d::{Curve, Line};

fn emit_result(valid: bool, divided: bool, snag: usize, negative: &Body, positive: &Body) {
    flag(valid);
    flag(divided);
    exact(snag);
    emit(negative);
    emit(positive);
}

fn rectangle(plane: Plane, min: [f64; 2], max: [f64; 2]) -> Body {
    let corners = [
        [min[0], min[1]],
        [max[0], min[1]],
        [max[0], max[1]],
        [min[0], max[1]],
    ];
    let boundary = (0..4)
        .map(|index| Curve::Line(Line {
            start: corners[index],
            end: corners[(index + 1) % 4],
        }))
        .collect::<Vec<_>>();
    planar_region(plane, &[boundary]).unwrap_or_default()
}

fn one_surface(surface: Surface, forward: bool) -> Body {
    let mut body = Body::new();
    let lump = body.lumps.insert(Lump {
        shells: Vec::new(),
        provenance: Provenance::Synthesized,
    });
    let shell = body.shells.insert(Shell {
        faces: Vec::new(),
        owner: lump,
        provenance: Provenance::Synthesized,
    });
    let surface = body.surfaces.insert(surface);
    let face = body.faces.insert(Face {
        surface,
        forward,
        loops: Vec::new(),
        owner: shell,
        provenance: Provenance::Synthesized,
    });
    body.lumps.get_mut(lump).unwrap().shells.push(shell);
    body.shells.get_mut(shell).unwrap().faces.push(face);
    body.roots.push(lump);
    body
}

fn retain_surface_kind(mut body: Body, kind: i32) -> Body {
    let keep = body.face_keys().find(|face| {
        let Some(node) = body.faces.get(*face) else { return false; };
        matches!(
            (kind, body.surfaces.get(node.surface)),
            (1, Some(Surface::Cylinder(_))) | (2, Some(Surface::Cone(_)))
        )
    }).unwrap();
    let removed = body.face_keys().filter(|face| *face != keep).collect::<Vec<_>>();
    for face in removed {
        let node = body.faces.get(face).unwrap().clone();
        for loop_key in node.loops {
            let ring = body.loops.get(loop_key).unwrap().clone();
            for coedge_key in ring.coedges {
                if let Some(coedge) = body.coedges.get(coedge_key).cloned() {
                    if let Some(edge) = body.edges.get_mut(coedge.edge) {
                        edge.coedges.retain(|candidate| *candidate != coedge_key);
                    }
                }
                body.coedges.remove(coedge_key);
            }
            body.loops.remove(loop_key);
        }
        body.faces.remove(face);
    }
    let owner = body.faces.get(keep).unwrap().owner;
    body.shells.get_mut(owner).unwrap().faces = vec![keep];
    body
}

fn curved_cutter(mode: i32, origin: [f64; 3], size: [f64; 3]) -> Body {
    let centre = [origin[0] + size[0] * 0.5, origin[1] + size[1] * 0.5, origin[2] + size[2] * 0.5];
    let radial = size[0].min(size[1]);
    match mode {
        15 => retain_surface_kind(
            make::cylinder(
                [centre[0], centre[1], origin[2] - size[2]],
                radial * 0.3,
                size[2] * 3.0,
            ).unwrap(),
            1,
        ),
        16 => retain_surface_kind(
            make::cone(
                [centre[0], centre[1], origin[2] - size[2]],
                radial * 0.45,
                size[2] * 3.0,
            ).unwrap(),
            2,
        ),
        17 => make::sphere(centre, size.into_iter().fold(0.0_f64, f64::max) * 0.65).unwrap(),
        18 => one_surface(
            Surface::Torus(Torus {
                frame: Plane::orthonormal(centre, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]).unwrap(),
                major_radius: radial * 0.3,
                minor_radius: radial * 0.12,
            }),
            true,
        ),
        _ => unreachable!(),
    }
}

fn main() {
    let values = std::env::args()
        .skip(1)
        .map(|value| value.parse::<f64>().unwrap())
        .collect::<Vec<_>>();
    let origin = [values[0], values[1], values[2]];
    let size = [values[3], values[4], values[5]];
    let mode = values[15] as i32;
    if (5..15).contains(&mode) {
        let Some(frame) = Plane::orthonormal(
            origin,
            [values[9], values[10], values[11]],
            [values[12], values[13], values[14]],
        ) else {
            flag(false);
            return;
        };
        let kind = (mode - 5) % 5;
        let forward = mode < 10;
        let surface = match kind {
            0 => Surface::Plane(frame),
            1 => Surface::Cylinder(Cylinder { base: frame, radius: size[0] }),
            2 => Surface::Cone(Cone { base: frame, radius: size[0], half_angle: size[1] }),
            3 => Surface::Sphere(Sphere { frame, radius: size[0] }),
            4 => Surface::Torus(Torus { frame, major_radius: size[0], minor_radius: size[1] }),
            _ => unreachable!(),
        };
        match surface_side(&one_surface(surface, forward), [values[6], values[7], values[8]]) {
            Some(value) => {
                flag(true);
                number(value);
            }
            None => flag(false),
        }
        return;
    }
    let Some(plane) = Plane::orthonormal(
        [values[6], values[7], values[8]],
        [values[9], values[10], values[11]],
        [values[12], values[13], values[14]],
    ) else {
        emit_result(false, false, 3, &Body::new(), &Body::new());
        return;
    };
    let body = if matches!(mode, 1 | 4 | 17 | 18) {
        let sheet_origin = if mode == 18 {
            [origin[0], origin[1], origin[2] + size[2] * 0.5]
        } else {
            origin
        };
        rectangle(
            Plane::orthonormal(sheet_origin, [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]).unwrap(),
            [0.0, 0.0],
            [size[0], size[1]],
        )
    } else {
        make::cuboid(origin, size).unwrap_or_default()
    };
    let sliced = if mode == 0 || mode == 1 {
        slice_by_plane(&body, plane)
    } else if mode >= 15 {
        slice_by_surface(&body, &curved_cutter(mode, origin, size))
    } else {
        let margin = size.into_iter().fold(1.0_f64, f64::max) * 4.0 + 1.0;
        let mut cutter = rectangle(plane, [-margin, -margin], [margin, margin]);
        if mode == 3 {
            let face = cutter.face_keys().next();
            if let Some(face) = face {
                if let Some(node) = cutter.faces.get_mut(face) {
                    node.forward = !node.forward;
                }
            }
        }
        slice_by_surface(&body, &cutter)
    };
    match sliced {
        Ok(Some(slice)) => emit_result(true, true, 0, &slice.negative, &slice.positive),
        Ok(None) => emit_result(true, false, 0, &Body::new(), &Body::new()),
        Err(snag) => emit_result(
            false,
            false,
            match snag {
                Snag::NoClosedForm => 1,
                Snag::Coincident => 2,
                Snag::CutRefused => 3,
            },
            &Body::new(),
            &Body::new(),
        ),
    }
}
