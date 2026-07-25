#!/usr/bin/env python3
"""Generate the deterministic Vitruvian micro-triangle point cloud with Blender."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
import random
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Vector


SOURCE_URL = "https://download.blender.org/demo/bundles/bundles-3.6/human_base_meshes_bundle.blend"
SOURCE_PAGE = "https://commons.wikimedia.org/wiki/File:Body_male_realistic_by_Dan_Ulrich_(CC0).stl"
SOURCE_SHA256 = "660e245812ef56768bc71293bd4c8bc1fd19a63710e16aafc8258a7351cbc35e"
BODY_OBJECT = "GEO-body_male_realistic"
EYE_OBJECTS = ("GEO-body_male_realistic.eye.L", "GEO-body_male_realistic.eye.R")
SEED = 0xD71E_2026


def arguments() -> argparse.Namespace:
    script_arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    return parser.parse_args(script_arguments)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def smoothstep(lower: float, upper: float, value: float) -> float:
    normalized = min(1.0, max(0.0, (value - lower) / (upper - lower)))
    return normalized * normalized * (3.0 - 2.0 * normalized)


def rotate_xz(point: Vector, pivot_x: float, pivot_z: float, angle: float) -> Vector:
    cosine = math.cos(angle)
    sine = math.sin(angle)
    relative_x = point.x - pivot_x
    relative_z = point.z - pivot_z
    return Vector(
        (
            pivot_x + relative_x * cosine - relative_z * sine,
            point.y,
            pivot_z + relative_x * sine + relative_z * cosine,
        )
    )


def limb_weights(point: Vector) -> tuple[float, float]:
    absolute_x = abs(point.x)
    arm_boundary = 0.18 + max(0.0, point.z - 1.12) * 0.18
    arm = smoothstep(arm_boundary, arm_boundary + 0.055, absolute_x)
    arm *= smoothstep(0.68, 0.82, point.z) * (1.0 - smoothstep(1.43, 1.53, point.z))
    leg = smoothstep(0.97, 0.78, point.z) * smoothstep(0.035, 0.12, absolute_x)
    return arm, leg


def posed(point: Vector, arm_angle: float, leg_angle: float) -> Vector:
    side = 1.0 if point.x >= 0.0 else -1.0
    arm_weight, leg_weight = limb_weights(point)
    transformed = rotate_xz(point, side * 0.225, 1.42, side * arm_angle * arm_weight)
    transformed = rotate_xz(transformed, side * 0.105, 0.91, side * leg_angle * leg_weight)
    return transformed


def normalized(point: Vector) -> tuple[float, float, float]:
    return point.x * 2.18, (point.z - 0.90) * 1.15, (point.y + 0.045) * -2.35


def triangle_sampler(mesh: bpy.types.Mesh, predicate=None):
    mesh.calc_loop_triangles()
    triangles: list[tuple[Vector, Vector, Vector]] = []
    cumulative_areas: list[float] = []
    total_area = 0.0
    for triangle in mesh.loop_triangles:
        vertices = tuple(mesh.vertices[index].co.copy() for index in triangle.vertices)
        centroid = sum(vertices, Vector()) / 3.0
        if predicate is not None and not predicate(centroid):
            continue
        area = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).length * 0.5
        if area <= 0.0:
            continue
        total_area += area
        triangles.append(vertices)
        cumulative_areas.append(total_area)
    if not triangles:
        raise RuntimeError("No source triangles matched the requested region")
    return triangles, cumulative_areas, total_area


def sample_triangle(sampler, rng: random.Random) -> Vector:
    triangles, cumulative_areas, total_area = sampler
    triangle = triangles[bisect.bisect_left(cumulative_areas, rng.random() * total_area)]
    first = rng.random()
    second = rng.random()
    if first + second > 1.0:
        first = 1.0 - first
        second = 1.0 - second
    return triangle[0] + (triangle[1] - triangle[0]) * first + (triangle[2] - triangle[0]) * second


def sampled_points(sampler, count: int, rng: random.Random, arm_angle: float, leg_angle: float):
    return [normalized(posed(sample_triangle(sampler, rng), arm_angle, leg_angle)) for _ in range(count)]


def append_frame(points: list[tuple[float, float, float, int]], count: int) -> None:
    for index in range(count):
        angle = math.tau * index / count
        points.append((math.cos(angle) * 1.08, math.sin(angle) * 1.08, 0.34, 4))
    edge_count = count // 4
    corners = ((-0.84, -1.04), (0.84, -1.04), (0.84, 1.00), (-0.84, 1.00))
    for edge in range(4):
        start = corners[edge]
        end = corners[(edge + 1) % 4]
        for index in range(edge_count):
            progress = index / edge_count
            points.append(
                (
                    start[0] + (end[0] - start[0]) * progress,
                    start[1] + (end[1] - start[1]) * progress,
                    0.32,
                    5,
                )
            )


def eye_points(body: bpy.types.Object) -> list[tuple[float, float, float]]:
    result = []
    inverse_body_translation = Vector((-body.location.x, -body.location.y, -body.location.z))
    for object_name in EYE_OBJECTS:
        eye = bpy.data.objects[object_name]
        for vertex in eye.data.vertices:
            body_local = eye.matrix_world @ vertex.co + inverse_body_translation
            result.append(normalized(body_local))
    return result


def write_geometry(path: Path, points: list[tuple[float, float, float, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + ".pending")
    with pending.open("wb") as output:
        for x, y, z, group in points:
            for corner in range(3):
                output.write(struct.pack("<ffff", x, y, z, float(group * 3 + corner)))
    pending.replace(path)


def write_metadata(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + ".pending")
    pending.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    pending.replace(path)


def main() -> None:
    options = arguments()
    actual_sha256 = sha256(options.blend)
    if actual_sha256 != SOURCE_SHA256:
        raise RuntimeError(f"Source blend SHA-256 is {actual_sha256}; expected {SOURCE_SHA256}")

    bpy.ops.wm.open_mainfile(filepath=str(options.blend))
    body = bpy.data.objects[BODY_OBJECT]
    mesh = body.data
    rng = random.Random(SEED)
    full_sampler = triangle_sampler(mesh)
    head_sampler = triangle_sampler(mesh, lambda point: point.z >= 1.49)

    surface_points = [normalized(posed(vertex.co.copy(), 1.05, 0.25)) for vertex in mesh.vertices]
    surface_points.extend(sampled_points(full_sampler, 12000, rng, 1.05, 0.25))
    head_points = sampled_points(head_sampler, 6500, rng, 1.05, 0.25)
    head_points.extend(eye_points(body))

    arm_candidates = [vertex.co.copy() for vertex in mesh.vertices if limb_weights(vertex.co)[0] > 0.55]
    leg_candidates = [vertex.co.copy() for vertex in mesh.vertices if limb_weights(vertex.co)[1] > 0.55]
    if len(arm_candidates) < 1000 or len(leg_candidates) < 1000:
        raise RuntimeError("Source mesh anatomy could not be segmented deterministically")

    alternate_arms = [
        normalized(posed(arm_candidates[rng.randrange(len(arm_candidates))], 1.48, 0.25))
        for _ in range(5000)
    ]
    alternate_legs = [
        normalized(posed(leg_candidates[rng.randrange(len(leg_candidates))], 1.05, 0.04))
        for _ in range(4000)
    ]

    interior_points = []
    for _ in range(7000):
        x, y, z = surface_points[rng.randrange(len(surface_points))]
        contraction = rng.uniform(0.12, 0.82)
        interior_points.append((x * contraction, y, z * contraction))

    points: list[tuple[float, float, float, int]] = []
    points.extend((*point, 0) for point in surface_points)
    points.extend((*point, 0) for point in head_points)
    points.extend((*point, 1) for point in interior_points)
    points.extend((*point, 2) for point in alternate_arms)
    points.extend((*point, 3) for point in alternate_legs)
    append_frame(points, 1800)
    write_geometry(options.output, points)

    metadata = {
        "schemaVersion": 1,
        "license": "CC0-1.0",
        "generatorSeed": SEED,
        "source": {
            "url": SOURCE_URL,
            "page": SOURCE_PAGE,
            "sha256": SOURCE_SHA256,
            "object": BODY_OBJECT,
            "eyeObjects": list(EYE_OBJECTS),
        },
        "format": "float32x4",
        "primitive": "triangles",
        "pointCount": len(points),
        "vertexCount": len(points) * 3,
        "surfacePointCount": len(surface_points) + len(head_points),
        "interiorPointCount": len(interior_points),
        "headPointCount": len(head_points),
        "alternateArmPointCount": len(alternate_arms),
        "alternateLegPointCount": len(alternate_legs),
        "framePointCount": 3600,
    }
    write_metadata(options.metadata, metadata)
    print(f"Generated {len(points)} volumetric points ({len(points) * 3} vertices)")


main()
