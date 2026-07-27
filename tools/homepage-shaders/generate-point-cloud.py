#!/usr/bin/env python3
"""Generate a deterministic density point cloud from the Vitruvian Man STL."""

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

import bmesh
import bpy
from mathutils import Vector


SOURCE_UID = "6c0b99ce8463468fbd00f304dbe7e105"
SOURCE_TITLE = "The Vitruvian Man"
SOURCE_AUTHOR = "Fri"
SOURCE_AUTHOR_URL = "https://sketchfab.com/manhiac"
SOURCE_URL = (
    "https://sketchfab.com/3d-models/"
    "the-vitruvian-man-6c0b99ce8463468fbd00f304dbe7e105"
)
SOURCE_LICENSE = "CC-BY-4.0"
SOURCE_LICENSE_URL = "https://creativecommons.org/licenses/by/4.0/"
SOURCE_ARCHIVE_SHA256 = (
    "7a00f51606aa4142484e7f3ba6d153bf7bdcec43da5640d1f73f082e95440a53"
)
SOURCE_MESH_SHA256 = (
    "093d1f10df3f7d1b1a1df7f92863966f54e379a168e0826ff15f187b7cf4f9b4"
)
SOURCE_VERTEX_COUNT = 241_794
SOURCE_TRIANGLE_COUNT = 483_637
SEED = 0xD71E_2026
TARGET_EXTENT = 2.16
VERTEX_POINT_COUNT = 24_000
SURFACE_SAMPLE_COUNT = 44_000
DENSITY_POINT_COUNT = 8_000
COORDINATE_MINIMUM = -2.0
COORDINATE_MAXIMUM = 2.0
QUANTIZATION_LEVELS = 4095
PACKING_RADIX = QUANTIZATION_LEVELS + 1
SPATIAL_PAIRING_LEAF_POINT_COUNT = 64
SPATIAL_PAIRING_AXIS_ORDER = ("x", "y", "z")
WHEEL_CORNER_OFFSET = 4


def arguments() -> argparse.Namespace:
    script_arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--stl", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--metadata", required=True, type=Path)
    return parser.parse_args(script_arguments)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_mesh(path: Path) -> bpy.types.Mesh:
    actual_sha256 = sha256(path)
    if actual_sha256 != SOURCE_MESH_SHA256:
        raise RuntimeError(
            f"Source STL SHA-256 is {actual_sha256}; expected {SOURCE_MESH_SHA256}"
        )

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.stl_import(filepath=str(path))
    imported = [item for item in bpy.context.selected_objects if item.type == "MESH"]
    if len(imported) != 1:
        raise RuntimeError("The pinned Vitruvian archive must contain exactly one mesh")

    mesh = imported[0].data
    mesh.validate(verbose=False, clean_customdata=True)
    if len(mesh.vertices) != SOURCE_VERTEX_COUNT:
        raise RuntimeError(
            f"Source STL has {len(mesh.vertices)} vertices; expected {SOURCE_VERTEX_COUNT}"
        )
    if len(mesh.polygons) != SOURCE_TRIANGLE_COUNT:
        raise RuntimeError(
            f"Source STL has {len(mesh.polygons)} triangles; expected {SOURCE_TRIANGLE_COUNT}"
        )

    editable = bmesh.new()
    editable.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(editable, faces=editable.faces)
    editable.to_mesh(mesh)
    editable.free()
    mesh.update()
    mesh.calc_loop_triangles()
    return mesh


def mesh_transform(mesh: bpy.types.Mesh) -> tuple[Vector, float, list[float], list[float]]:
    minimum = Vector(
        tuple(min(vertex.co[axis] for vertex in mesh.vertices) for axis in range(3))
    )
    maximum = Vector(
        tuple(max(vertex.co[axis] for vertex in mesh.vertices) for axis in range(3))
    )
    center = (minimum + maximum) * 0.5
    planar_extent = max(maximum.x - minimum.x, maximum.z - minimum.z)
    if planar_extent <= 0.0:
        raise RuntimeError("Source STL has no planar extent")
    return center, TARGET_EXTENT / planar_extent, list(minimum), list(maximum)


def normalized(point: Vector, center: Vector, scale: float) -> tuple[float, float, float]:
    return (
        (point.x - center.x) * scale,
        (point.z - center.z) * scale,
        (center.y - point.y) * scale,
    )


def triangle_sampler(mesh: bpy.types.Mesh):
    triangles: list[tuple[tuple[int, int, int], Vector]] = []
    cumulative_areas: list[float] = []
    total_area = 0.0
    for triangle in mesh.loop_triangles:
        indices = tuple(triangle.vertices)
        vertices = tuple(mesh.vertices[index].co for index in indices)
        area = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]).length * 0.5
        if area <= 0.0:
            continue
        total_area += area
        triangles.append((indices, triangle.normal.copy()))
        cumulative_areas.append(total_area)
    if not triangles:
        raise RuntimeError("Source STL has no non-degenerate triangles")
    return triangles, cumulative_areas, total_area


def sample_triangle(
    mesh: bpy.types.Mesh,
    sampler,
    rng: random.Random,
) -> tuple[Vector, Vector]:
    triangles, cumulative_areas, total_area = sampler
    triangle_index = bisect.bisect_left(cumulative_areas, rng.random() * total_area)
    indices, normal = triangles[triangle_index]
    vertices = tuple(mesh.vertices[index].co for index in indices)
    first = rng.random()
    second = rng.random()
    if first + second > 1.0:
        first = 1.0 - first
        second = 1.0 - second
    point = (
        vertices[0]
        + (vertices[1] - vertices[0]) * first
        + (vertices[2] - vertices[0]) * second
    )
    return point, normal


def deterministic_vertex_points(
    mesh: bpy.types.Mesh,
    center: Vector,
    scale: float,
) -> list[tuple[float, float, float]]:
    vertex_count = len(mesh.vertices)
    step = 104_729
    while math.gcd(step, vertex_count) != 1:
        step += 2
    index = SEED % vertex_count
    result = []
    for _ in range(VERTEX_POINT_COUNT):
        result.append(normalized(mesh.vertices[index].co, center, scale))
        index = (index + step) % vertex_count
    return result


def sampled_surface_points(
    mesh: bpy.types.Mesh,
    sampler,
    count: int,
    rng: random.Random,
    center: Vector,
    scale: float,
) -> list[tuple[float, float, float]]:
    return [
        normalized(sample_triangle(mesh, sampler, rng)[0], center, scale)
        for _ in range(count)
    ]


def sampled_density_points(
    mesh: bpy.types.Mesh,
    sampler,
    count: int,
    rng: random.Random,
    center: Vector,
    scale: float,
) -> list[tuple[float, float, float]]:
    result = []
    for _ in range(count):
        point, normal = sample_triangle(mesh, sampler, rng)
        depth = rng.uniform(0.012, 0.055)
        result.append(normalized(point - normal * depth, center, scale))
    return result


def sample_torus(
    center: tuple[float, float, float],
    major_radius: float,
    tube_radius: float,
    depth_radius: float,
    rng: random.Random,
) -> tuple[float, float, float]:
    ring_angle = rng.random() * math.tau
    tube_angle = rng.random() * math.tau
    radial = major_radius + math.cos(tube_angle) * tube_radius
    return (
        center[0] + math.cos(ring_angle) * radial,
        center[1] + math.sin(ring_angle) * radial,
        center[2] + math.sin(tube_angle) * depth_radius,
    )


def sample_ellipsoid(
    center: tuple[float, float, float],
    radii: tuple[float, float, float],
    rng: random.Random,
) -> tuple[float, float, float]:
    height = rng.uniform(-1.0, 1.0)
    ring = math.sqrt(max(0.0, 1.0 - height * height))
    angle = rng.random() * math.tau
    return (
        center[0] + math.cos(angle) * ring * radii[0],
        center[1] + math.sin(angle) * ring * radii[1],
        center[2] + height * radii[2],
    )


def sample_segment_tube(
    start: tuple[float, float, float],
    end: tuple[float, float, float],
    radius: float,
    rng: random.Random,
) -> tuple[float, float, float]:
    start_vector = Vector(start)
    axis = Vector(end) - start_vector
    if axis.length <= 0.0:
        raise RuntimeError("Motorcycle tube segment has no length")
    tangent = axis.normalized()
    reference = Vector((0.0, 0.0, 1.0))
    if abs(tangent.dot(reference)) > 0.9:
        reference = Vector((0.0, 1.0, 0.0))
    normal = tangent.cross(reference).normalized()
    binormal = tangent.cross(normal).normalized()
    angle = rng.random() * math.tau
    offset = normal * (math.cos(angle) * radius)
    offset += binormal * (math.sin(angle) * radius)
    point = start_vector + axis * rng.random() + offset
    return tuple(point)


def motorcycle_points(
    count: int,
    rng: random.Random,
) -> list[tuple[float, float, float, bool]]:
    points: list[tuple[float, float, float, bool]] = []

    tire_count = round(count * 0.35)
    for index in range(tire_count):
        center_x = -0.72 if index < tire_count // 2 else 0.72
        point = sample_torus(
            (center_x, -0.42, 0.0),
            0.30,
            0.055,
            0.072,
            rng,
        )
        points.append((*point, True))

    hub_count = round(count * 0.05)
    for index in range(hub_count):
        center_x = -0.72 if index < hub_count // 2 else 0.72
        point = sample_ellipsoid(
            (center_x, -0.42, 0.0),
            (0.095, 0.095, 0.13),
            rng,
        )
        points.append((*point, True))

    spoke_count = round(count * 0.07)
    for index in range(spoke_count):
        center_x = -0.72 if index % 2 == 0 else 0.72
        angle = rng.randrange(8) * (math.tau / 8.0)
        reach = (math.cos(angle) * 0.275, math.sin(angle) * 0.275)
        point = sample_segment_tube(
            (center_x, -0.42, 0.0),
            (center_x + reach[0], -0.42 + reach[1], 0.0),
            0.018,
            rng,
        )
        points.append((*point, True))

    shell_count = round(count * 0.18)
    shells = (
        ((-0.08, -0.03, 0.0), (0.34, 0.22, 0.25), 0.38),
        ((0.10, 0.19, 0.0), (0.36, 0.18, 0.23), 0.38),
        ((-0.40, 0.24, 0.0), (0.31, 0.075, 0.22), 0.17),
        ((0.78, 0.08, 0.0), (0.085, 0.085, 0.105), 0.07),
    )
    shell_boundaries = []
    cumulative_weight = 0.0
    for _, _, weight in shells:
        cumulative_weight += weight
        shell_boundaries.append(cumulative_weight)
    for _ in range(shell_count):
        selection = rng.random()
        shell_index = bisect.bisect_left(shell_boundaries, selection)
        center, radii, _ = shells[shell_index]
        points.append((*sample_ellipsoid(center, radii, rng), False))

    frame_count = round(count * 0.15)
    frame_segments = (
        ((-0.72, -0.42, 0.0), (-0.18, 0.08, 0.0), 0.045),
        ((-0.18, 0.08, 0.0), (0.72, -0.42, 0.0), 0.045),
        ((0.72, -0.42, 0.0), (-0.34, -0.42, 0.0), 0.045),
        ((-0.34, -0.42, 0.0), (-0.18, 0.08, 0.0), 0.045),
        ((0.72, -0.42, -0.07), (0.56, 0.30, -0.07), 0.032),
        ((0.72, -0.42, 0.07), (0.56, 0.30, 0.07), 0.032),
        ((0.56, 0.30, -0.30), (0.56, 0.30, 0.30), 0.028),
        ((-0.34, -0.18, -0.12), (-0.72, -0.25, -0.12), 0.038),
    )
    for index in range(frame_count):
        start, end, radius = frame_segments[index % len(frame_segments)]
        points.append((*sample_segment_tube(start, end, radius, rng), False))

    rider_count = count - len(points)
    rider_head_count = round(rider_count * 0.17)
    rider_torso_count = round(rider_count * 0.33)
    rider_limb_count = rider_count - rider_head_count - rider_torso_count
    for _ in range(rider_head_count):
        point = sample_ellipsoid(
            (-0.10, 0.78, 0.0),
            (0.15, 0.17, 0.15),
            rng,
        )
        points.append((*point, False))
    for _ in range(rider_torso_count):
        point = sample_ellipsoid(
            (-0.18, 0.48, 0.0),
            (0.24, 0.35, 0.18),
            rng,
        )
        points.append((*point, False))
    rider_segments = (
        ((-0.02, 0.59, -0.11), (0.37, 0.30, -0.15), 0.075),
        ((-0.02, 0.59, 0.11), (0.37, 0.30, 0.15), 0.075),
        ((0.37, 0.30, -0.15), (0.73, 0.27, -0.22), 0.055),
        ((0.37, 0.30, 0.15), (0.73, 0.27, 0.22), 0.055),
        ((-0.14, 0.24, -0.10), (-0.40, -0.18, -0.13), 0.095),
        ((-0.14, 0.24, 0.10), (-0.40, -0.18, 0.13), 0.095),
        ((-0.40, -0.18, -0.13), (-0.62, -0.34, -0.10), 0.075),
        ((-0.40, -0.18, 0.13), (-0.62, -0.34, 0.10), 0.075),
    )
    for index in range(rider_limb_count):
        start, end, radius = rider_segments[index % len(rider_segments)]
        points.append((*sample_segment_tube(start, end, radius, rng), False))

    if len(points) != count:
        raise RuntimeError(f"Generated {len(points)} motorcycle points; expected {count}")
    rng.shuffle(points)
    return points


def spatially_pair_points(
    targets: list[tuple[float, float, float]],
    motorcycle: list[tuple[float, float, float, bool]],
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float, bool]],
]:
    if len(targets) != len(motorcycle):
        raise RuntimeError("Motorcycle and Vitruvian point populations must be identical")

    paired_targets: list[tuple[float, float, float]] = []
    paired_motorcycle: list[tuple[float, float, float, bool]] = []

    def pair_region(
        target_region: list[tuple[float, float, float]],
        motorcycle_region: list[tuple[float, float, float, bool]],
        depth: int,
    ) -> None:
        axis_order = tuple((depth + offset) % 3 for offset in range(3))
        coordinate_key = lambda point: tuple(point[axis] for axis in axis_order)
        target_region.sort(key=coordinate_key)
        motorcycle_region.sort(key=coordinate_key)
        if len(target_region) <= SPATIAL_PAIRING_LEAF_POINT_COUNT:
            paired_targets.extend(target_region)
            paired_motorcycle.extend(motorcycle_region)
            return

        midpoint = len(target_region) // 2
        pair_region(
            target_region[:midpoint],
            motorcycle_region[:midpoint],
            depth + 1,
        )
        pair_region(
            target_region[midpoint:],
            motorcycle_region[midpoint:],
            depth + 1,
        )

    pair_region(list(targets), list(motorcycle), 0)
    if len(paired_targets) != len(targets):
        raise RuntimeError("Spatial point pairing did not preserve the point population")
    return paired_targets, paired_motorcycle


def quantized_coordinate(value: float) -> int:
    if not COORDINATE_MINIMUM <= value <= COORDINATE_MAXIMUM:
        raise RuntimeError(f"Point coordinate {value} exceeds the packed geometry range")
    normalized_value = (
        (value - COORDINATE_MINIMUM)
        / (COORDINATE_MAXIMUM - COORDINATE_MINIMUM)
    )
    return round(normalized_value * QUANTIZATION_LEVELS)


def pack_coordinate_pair(target: float, motorcycle: float) -> float:
    packed = quantized_coordinate(target) * PACKING_RADIX
    packed += quantized_coordinate(motorcycle)
    return float(packed)


def write_geometry(
    path: Path,
    targets: list[tuple[float, float, float]],
    motorcycle: list[tuple[float, float, float, bool]],
) -> None:
    if len(targets) != len(motorcycle):
        raise RuntimeError("Motorcycle and Vitruvian point populations must be identical")
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + ".pending")
    with pending.open("wb") as output:
        for target, motorcycle_point in zip(targets, motorcycle, strict=True):
            packed = tuple(
                pack_coordinate_pair(target[axis], motorcycle_point[axis])
                for axis in range(3)
            )
            wheel_offset = WHEEL_CORNER_OFFSET if motorcycle_point[3] else 0
            for corner in range(3):
                encoded_corner = wheel_offset + corner
                output.write(struct.pack("<ffff", *packed, float(encoded_corner)))
    pending.replace(path)


def write_metadata(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + ".pending")
    pending.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    pending.replace(path)


def main() -> None:
    options = arguments()
    mesh = source_mesh(options.stl)
    center, scale, source_minimum, source_maximum = mesh_transform(mesh)
    rng = random.Random(SEED)
    full_sampler = triangle_sampler(mesh)

    vertex_points = deterministic_vertex_points(mesh, center, scale)
    surface_samples = sampled_surface_points(
        mesh,
        full_sampler,
        SURFACE_SAMPLE_COUNT,
        rng,
        center,
        scale,
    )
    density_points = sampled_density_points(
        mesh,
        full_sampler,
        DENSITY_POINT_COUNT,
        rng,
        center,
        scale,
    )

    points = vertex_points + surface_samples + density_points
    motorcycle = motorcycle_points(len(points), random.Random(SEED ^ 0x4D4F_544F))
    points, motorcycle = spatially_pair_points(points, motorcycle)
    write_geometry(options.output, points, motorcycle)

    metadata = {
        "schemaVersion": 5,
        "generatorSeed": SEED,
        "attributeEncoding": "paired-unorm12-wheel-corner",
        "coordinateEncoding": {
            "name": "paired-unorm12",
            "quantizationLevels": QUANTIZATION_LEVELS,
            "coordinateMinimum": COORDINATE_MINIMUM,
            "coordinateMaximum": COORDINATE_MAXIMUM,
        },
        "triangleCornerEncoding": {
            "name": "wheel-part-plus-corner",
            "wheelOffset": WHEEL_CORNER_OFFSET,
            "cornerCount": 3,
        },
        "pointPairing": {
            "name": "recursive-spatial-bisection",
            "leafPointCount": SPATIAL_PAIRING_LEAF_POINT_COUNT,
            "axisOrder": list(SPATIAL_PAIRING_AXIS_ORDER),
        },
        "source": {
            "uid": SOURCE_UID,
            "title": SOURCE_TITLE,
            "author": SOURCE_AUTHOR,
            "authorUrl": SOURCE_AUTHOR_URL,
            "url": SOURCE_URL,
            "license": SOURCE_LICENSE,
            "licenseUrl": SOURCE_LICENSE_URL,
            "archiveSha256": SOURCE_ARCHIVE_SHA256,
            "meshSha256": SOURCE_MESH_SHA256,
            "meshFile": "Vitruvian.stl",
            "modelVertexCount": SOURCE_VERTEX_COUNT,
            "modelTriangleCount": SOURCE_TRIANGLE_COUNT,
            "bounds": {
                "minimum": source_minimum,
                "maximum": source_maximum,
            },
        },
        "modifications": [
            "Centered and uniformly scaled from the pinned STL.",
            "Converted to deterministic surface and inward-density points.",
            "Paired spatially neighboring target and motorcycle regions recursively.",
            "Tagged motorcycle wheel points for vertex-stage rotation.",
            "Expanded into micro-triangles for the DynLex WebGL renderer.",
        ],
        "format": "float32x4",
        "primitive": "triangles",
        "pointCount": len(points),
        "motorcyclePointCount": len(motorcycle),
        "motorcycleWheelPointCount": sum(
            1 for motorcycle_point in motorcycle if motorcycle_point[3]
        ),
        "vertexCount": len(points) * 3,
        "surfacePointCount": len(vertex_points) + len(surface_samples),
        "densityPointCount": len(density_points),
    }
    write_metadata(options.metadata, metadata)
    print(f"Generated {len(points)} volumetric points ({len(points) * 3} vertices)")


main()
