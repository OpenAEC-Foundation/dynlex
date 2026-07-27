import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(testDir, "../..");
const toolDir = path.join(projectDir, "tools/homepage-shaders");
const expectedToolFiles = [
  "README.md",
  "compiler.mjs",
  "config.mjs",
  "generate.mjs",
  "generate-point-cloud.py"
];

for (const relativePath of expectedToolFiles) {
  const filePath = path.join(toolDir, relativePath);
  assert.ok(fs.existsSync(filePath), `Missing homepage shader tool: ${relativePath}`);
  const lineCount = fs.readFileSync(filePath, "utf8").split("\n").length;
  assert.ok(lineCount < 1000, `${relativePath} must stay under 1000 lines`);
}

const { shaderConfig } = await import(pathToFileURL(path.join(toolDir, "config.mjs")).href);
const { resolveTerrainGeometryDescriptor } = await import(pathToFileURL(
  path.join(projectDir, "web/terrain-geometry.js")
).href);
assert.ok(shaderConfig.durationSeconds >= 8);
assert.equal(shaderConfig.manifest, "web/shaders/manifest.json");
assert.ok(Array.isArray(shaderConfig.scenes) && shaderConfig.scenes.length >= 3);
assert.deepEqual(
  shaderConfig.scenes.map((scene) => scene.id),
  ["event-horizon", "endless-terrain", "nano-choreography"],
  "The banner must contain the three requested shader experiences"
);
assert.equal(new Set(shaderConfig.scenes.map((scene) => scene.id)).size, shaderConfig.scenes.length);

for (const scene of shaderConfig.scenes) {
  assert.match(scene.id, /^[a-z0-9]+(?:-[a-z0-9]+)*$/);
  assert.ok(scene.title.length > 0);
  assert.ok(fs.existsSync(path.join(projectDir, scene.source)), `Missing shader source: ${scene.source}`);
  assert.ok(fs.existsSync(path.join(projectDir, scene.fragment)), `Missing generated GLSL: ${scene.fragment}`);
  assert.ok(scene.fragment.startsWith("web/shaders/generated/"));
}

const shaderSources = shaderConfig.scenes.map((scene) => (
  fs.readFileSync(path.join(projectDir, scene.source), "utf8")
));
const sharedSource = fs.readFileSync(path.join(projectDir, "lib/shader_art.dl"), "utf8");
for (const [index, source] of shaderSources.entries()) {
  assert.doesNotMatch(
    source,
    /\b(?:cell_x|cell_y|grid_x|grid_y|value noise|fractal noise)\b/i,
    `${shaderConfig.scenes[index].id} must not reconstruct a visible square lattice`
  );
}
assert.doesNotMatch(sharedSource, /function (?:value|fractal) noise\b/i);
assert.doesNotMatch(sharedSource, /\bcell_[xy]\b/i);
assert.match(
  sharedSource,
  /function simplex field at x y phase/,
  "Shared shader art must provide a non-square procedural field"
);

const terrainSource = shaderSources[1];
assert.match(terrainSource, /function terrain height at x z/);
assert.match(terrainSource, /if this is a vertex shader/);
assert.match(terrainSource, /set the shader interpolant named "terrain_position"/);
assert.match(terrainSource, /set the shader interpolant named "terrain_normal"/);
assert.match(terrainSource, /set the shader interpolant named "terrain_material"/);
assert.match(terrainSource, /the shader interpolant [xyzw] named "terrain_position"/);
assert.match(terrainSource, /the shader interpolant [xyz] named "terrain_normal"/);
assert.match(terrainSource, /the shader interpolant x named "terrain_material"/);
assert.match(terrainSource, /the shader interpolant y named "terrain_material"/);
assert.match(terrainSource, /set displaced_height to terrain height at world_x world_z/);
assert.match(terrainSource, /set ray_distance to the vertex y/);
assert.match(terrainSource, /function terrain maximum possible height:\s+execute:\s+return 12\.10/);
assert.match(terrainSource, /function terrain camera altitude:\s+execute:\s+return \(terrain maximum possible height\) \+ 0\.70/);
assert.equal(
  (terrainSource.match(/set camera_y to terrain camera altitude/g) ?? []).length,
  2,
  "Vertex and fragment water stages must share one fixed camera altitude"
);
assert.match(terrainSource, /set camera_pitch to 0\.155/);
assert.match(terrainSource, /set base_vertical_distance to 0\.0 - camera_y/);
assert.doesNotMatch(
  terrainSource,
  /\bcamera_ground\b/,
  "The camera and its sampling plane must not follow terrain elevation"
);
assert.match(terrainSource, /set ray_slope to \(\(the vertex x\) \* aspect\) \* 0\.80/);
assert.match(terrainSource, /set ray_forward_scale to the square root of \(1\.0 \+ \(\(ray_slope \* pitch_cosine\) \* \(ray_slope \* pitch_cosine\)\)\)/);
assert.match(terrainSource, /set forward_distance to ray_distance \/ ray_forward_scale/);
assert.match(terrainSource, /set base_view_depth to \(forward_distance \* pitch_cosine\) - \(base_vertical_distance \* pitch_sine\)/);
assert.match(terrainSource, /set lateral_distance to ray_slope \* base_view_depth/);
assert.match(terrainSource, /set the shader interpolant named "terrain_normal" to normal_x normal_y normal_z ray_distance/);
assert.match(terrainSource, /set clip_z to view_z \* 1\.00078 - 0\.40016/);
assert.match(terrainSource, /set water_fog to smooth transition from 232\.0 to 376\.0 at ray_distance/);
assert.match(terrainSource, /set fog to smooth transition from 188\.0 to 376\.0 at ray_distance/);
assert.doesNotMatch(
  terrainSource,
  /\bview_distance\b/,
  "Terrain distance semantics must not mix radial and forward distances"
);
assert.doesNotMatch(
  terrainSource,
  /\b(?:depth_fraction|near_spread)\b/,
  "Terrain rows must represent radial distances and columns must remain fixed camera rays"
);
assert.match(terrainSource, /set mountain_ridge /);
assert.match(terrainSource, /set erosion_channels /);
assert.match(terrainSource, /function water detail visibility at distance:\s+execute:\s+return 1\.0 - \(smooth transition from 48\.0 to 96\.0 at distance\)/);
assert.match(terrainSource, /set normal_step to 0\.34/);
assert.match(terrainSource, /set height_left to terrain height at \(world_x - normal_step\) world_z/);
assert.match(terrainSource, /set height_right to terrain height at \(world_x \+ normal_step\) world_z/);
assert.match(terrainSource, /set height_back to terrain height at world_x \(world_z - normal_step\)/);
assert.match(terrainSource, /set height_front to terrain height at world_x \(world_z \+ normal_step\)/);
assert.match(terrainSource, /set normal_x to height_left - height_right/);
assert.match(terrainSource, /set normal_y to normal_step \* 2\.0/);
assert.match(terrainSource, /set normal_z to height_back - height_front/);
assert.doesNotMatch(
  terrainSource,
  /set normal_step to .*\bray_distance\b/,
  "Terrain normals must use one centered world-space gradient at every LOD"
);
assert.doesNotMatch(
  terrainSource,
  /\bstrata\b/,
  "Mountain materials must not paint contour bands over the smooth terrain normals"
);
assert.match(terrainSource, /set exposed_rock /);
assert.match(terrainSource, /set snow /);
assert.match(terrainSource, /surface_vertex > 1\.5/);
assert.match(terrainSource, /set water_level /);
assert.match(terrainSource, /set water_geometry_visibility to water detail visibility at ray_distance/);
assert.match(terrainSource, /set surface_variation to 0\.5 \+ \(\(\(water_wave_x \+ water_wave_z\) \* 0\.25\) \* water_geometry_visibility\)/);
assert.match(terrainSource, /set surface_detail to 0\.5 \+ \(\(water_wave_cross \* 0\.5\) \* water_geometry_visibility\)/);
assert.match(terrainSource, /set water_depth to water_level - \(terrain height at world_x world_z\)/);
assert.match(terrainSource, /the shader interpolant z named "terrain_material"/);
assert.match(
  terrainSource,
  /set shallow_water to \(1\.0 - \(smooth transition from 0\.18 to 3\.8 at water_depth\)\) \* water_ripple_visibility/
);
assert.match(
  terrainSource,
  /set water_caustic_depth to \(1\.0 - \(smooth transition from 0\.05 to 0\.65 at water_depth\)\) \* water_ripple_visibility/
);
assert.match(terrainSource, /set water_caustic /);
assert.match(
  terrainSource,
  /set water_ripple_visibility to water detail visibility at ray_distance/
);
assert.match(terrainSource, /set water_view_facing /);
assert.match(terrainSource, /set fresnel_grazing to 1\.0 - water_view_facing/);
assert.match(terrainSource, /set water_fresnel to 0\.020 \+/);
assert.match(terrainSource, /set reflected_sky_height /);
assert.match(
  terrainSource,
  /set reflected_sun_alignment to saturate \(\(\(reflected_x \* 0\.39\) \+ \(reflected_y \* 0\.32\)\) \+ \(reflected_z \* 0\.86\)\)/
);
assert.match(terrainSource, /set water_sun_glow /);
assert.match(terrainSource, /set water_sun_glint /);
assert.doesNotMatch(
  terrainSource,
  /\bwater_half_[xyz]\b/,
  "Water must evaluate the sun against the reflected view ray directly"
);
assert.doesNotMatch(
  terrainSource,
  /\bwater_shimmer\b/,
  "Water highlights must come from reflected light rather than a broad white threshold"
);
assert.match(terrainSource, /simplex field at/);
assert.doesNotMatch(
  terrainSource,
  /\b(?:signed flow|ridged field) at\b/,
  "Terrain must not be assembled from periodic wave ridges"
);
assert.doesNotMatch(terrainSource, /\b(?:march_step|terrain_hit|hit_distance|refinement|ray_step)\b/);
assert.doesNotMatch(terrainSource, /\briver\b/i);

const nanoSource = shaderSources[2];
assert.equal(
  (nanoSource.match(/set moment to the minimum of time and 10\.40/g) ?? []).length,
  2,
  "The one-shot nano choreography must hold its completed Vitruvian frame"
);
assert.doesNotMatch(
  nanoSource,
  /set moment to \(fractional part of \(time \/ 11\.0\)\) \* 11\.0/,
  "The nano choreography must not loop back to the motorcycle before the next scene covers it"
);
assert.doesNotMatch(
  nanoSource,
  /\bcrystal(?: distance|_visibility|_light)\b/i,
  "The nano sequence must move directly between the motorcycle and Vitruvian figure"
);
for (const threeDimensionalDetail of [
  "motorcycle_yaw",
  "motorcycle_wheel_spin",
  "wheel_point",
  "packed_x",
  "target_quantized_x",
  "motorcycle_quantized_x",
  "motorcycle_local_x",
  "motorcycle_ndc_x",
  "target_ndc_x",
  "this is a vertex shader",
  "shader render pass",
  "persistent point"
]) {
  assert.match(
    nanoSource,
    new RegExp(threeDimensionalDetail.replaceAll(" ", "\\s+")),
    `Nano choreography must define ${threeDimensionalDetail.replaceAll("_", " ")}`
  );
}
assert.doesNotMatch(
  nanoSource,
  /motorcycle distance at x y z|while ray_step|surface_drones|surface_hit|hologram_glow|motorcycle_part|shell_center_x|frame_start_x|rider_center_x/,
  "The motorcycle must come from paired geometry points, not a raymarch or runtime geometry generator"
);
const pointLight = Object.fromEntries(
  ["red", "green", "blue"].map((channel) => {
    const match = nanoSource.match(
      new RegExp(`set point_${channel} to ([\\d.]+) \\+ [^\\n]+ \\* ([\\d.]+)`)
    );
    assert.ok(match, `Vitruvian ${channel} point-light channel must be explicit`);
    return [channel, Number(match[1]) + Number(match[2])];
  })
);
assert.ok(pointLight.red >= 0.25, "Vitruvian red points must remain visible");
assert.ok(pointLight.green >= 0.4, "Vitruvian green points must remain visible");
assert.ok(pointLight.blue >= 0.75, "Vitruvian blue points must remain visible");
assert.match(
  nanoSource,
  /set relative_point_size to 0\.00104 \* \(0\.96 \+ render_pass \* 0\.04\)/,
  "Every volumetric point must use the same viewport-relative footprint"
);
assert.match(
  nanoSource,
  /set point_size_x to relative_point_size \/ aspect/,
  "Point width must compensate for viewport aspect ratio"
);
assert.match(
  nanoSource,
  /set point_size_y to relative_point_size/,
  "Point height must remain a fixed fraction of viewport height"
);
assert.doesNotMatch(
  nanoSource,
  /point_size to [^\n]*\/ width/,
  "Point size must not remain fixed in physical framebuffer pixels"
);
assert.match(
  nanoSource,
  /set triangle_corner to encoded_triangle_corner - \(wheel_point \* 4\.0\)/,
  "Geometry must decode the wheel flag without changing the triangle corner"
);
assert.doesNotMatch(
  nanoSource,
  /\b(?:point_group|build_wave)\b/,
  "The figure must not use region-specific weights or a build-wave visibility mask"
);
for (const swarmMotion of [
  "drone_delay",
  "assembly_progress",
  "flight_arc",
  "assembled_ndc_x",
  "assembled_ndc_y"
]) {
  assert.match(
    nanoSource,
    new RegExp(`\\b${swarmMotion}\\b`),
    `The motorcycle drones must define ${swarmMotion.replaceAll("_", " ")}`
  );
}
assert.match(
  nanoSource,
  /set assembly_progress to smooth transition from \(4\.45 \+ drone_delay\) to \(6\.65 \+ drone_delay\) at moment/,
  "Each drone must receive a long, stable, staggered flight window"
);
assert.match(
  nanoSource,
  /set assembled_ndc_x to motorcycle_ndc_x \+ \(target_ndc_x - motorcycle_ndc_x\) \* assembly_progress \+ flight_x/,
  "The same points must fly directly from motorcycle positions into their final model positions"
);
assert.match(
  nanoSource,
  /set flight_arc to \(the sine of \(assembly_progress \* 3\.14159265\)\)/,
  "Curved flight must converge exactly at both endpoint shapes"
);
assert.match(
  nanoSource,
  /set drone_seed_a to \(\(the sine of \(\(\(point_x \* 3\.13\) \+ \(point_y \* 2\.71\)\) \+ \(point_z \* 4\.19\)\)\) \* 0\.5\) \+ 0\.5/,
  "Neighboring drones must follow a continuous flock field instead of random-looking paths"
);
assert.doesNotMatch(
  nanoSource,
  /\b(?:swarm_x|swarm_y|swarm_z)\b/,
  "The transition must not pass through an unrelated synthetic cloud"
);
assert.doesNotMatch(
  nanoSource,
  /\bfigure_offset_x\b/,
  "The Vitruvian figure must not receive a horizontal viewport offset"
);
assert.match(
  nanoSource,
  /set target_ndc_x to \(target_turned_x \* horizontal_scale\) \/ target_depth/,
  "The Vitruvian target must remain centered in its perspective projection"
);
assert.match(
  nanoSource,
  /set motorcycle_yaw to 1\.30 \+ \(motorcycle_progress \* 0\.04\)/,
  "The motorcycle front must lead its movement toward the camera"
);
assert.match(
  nanoSource,
  /set motorcycle_ndc_x to \(motorcycle_world_x \* 1\.72\) \/ \(motorcycle_depth \* \(the maximum of aspect and 1\.0\)\)/,
  "The motorcycle must use a perspective divide after its world transform"
);
assert.match(
  nanoSource,
  /set clip_x to \(assembled_ndc_x \+ corner_x\) \* depth/,
  "Projected motorcycle points must return to homogeneous clip space"
);
for (const radiusMeasurement of [
  "measurement_angle",
  "radius_center_y",
  "radius_end_x",
  "radius_end_y",
  "radius_line",
  "radius_tick",
  "circle_highlight"
]) {
  assert.match(
    nanoSource,
    new RegExp(`\\b${radiusMeasurement}\\b`),
    `The Vitruvian measurement must define ${radiusMeasurement.replaceAll("_", " ")}`
  );
}
assert.match(
  nanoSource,
  /distance from point screen_x screen_y to segment 0\.0 radius_center_y radius_end_x radius_end_y/,
  "The measurement line must extend from the model center to the circle"
);
assert.match(
  nanoSource,
  /set radius_end_x to circle_turned_x \* 1\.66 \/ circle_depth/,
  "The radius endpoint must use the same perspective projection as the circle"
);
assert.match(
  nanoSource,
  /set measurement_visibility to smooth transition from 7\.55 to 8\.10 at moment/,
  "The measurement must remain visible for the completed one-shot composition"
);
assert.match(
  nanoSource,
  /set measurement_angle to \(time - 7\.55\) \* 1\.32/,
  "The measurement must keep rotating after the one-shot choreography clock stops"
);
assert.doesNotMatch(
  nanoSource,
  /set measurement_angle to \(moment -/,
  "The measurement angle must not use the clamped choreography clock"
);
assert.doesNotMatch(
  nanoSource,
  /function vitruvian distance at/,
  "The model-derived Vitruvian point cloud must replace the analytic body approximation"
);
assert.doesNotMatch(
  nanoSource,
  /distance from point bike_x bike_y to segment|distance from point human_x human_y to segment/,
  "The motorcycle and Vitruvian figure must not be 2D line drawings"
);
for (const sharedThreeDimensionalPrimitive of [
  "function distance from three dimensional point x y z to capsule",
  "function ellipsoid distance at x y z",
  "function torus distance at x y z"
]) {
  assert.match(
    sharedSource,
    new RegExp(sharedThreeDimensionalPrimitive.replaceAll(" ", "\\s+")),
    `Shared shader art must provide ${sharedThreeDimensionalPrimitive}`
  );
}

const manifestPath = path.join(projectDir, shaderConfig.manifest);
assert.ok(fs.existsSync(manifestPath), "Missing generated shader manifest");
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
assert.equal(manifest.schemaVersion, 9);
assert.deepEqual(manifest.semanticLegend, JSON.parse(
  JSON.stringify(manifest.semanticLegend)
));
assert.equal(manifest.scenes.length, shaderConfig.scenes.length);
assert.deepEqual(manifest.scenes.map((scene) => scene.id), shaderConfig.scenes.map((scene) => scene.id));
for (let index = 0; index < manifest.scenes.length; index += 1) {
  const record = manifest.scenes[index];
  const configured = shaderConfig.scenes[index];
  assert.equal(record.source, fs.readFileSync(path.join(projectDir, configured.source), "utf8"));
  assert.match(record.sourceHash, /^[a-f0-9]{64}$/);
  assert.equal(record.shaders.fragment.path, configured.fragment.replace(/^web\//, ""));
  assert.match(record.shaders.fragment.hash, /^[a-f0-9]{64}$/);
  assert.ok(Array.isArray(record.semanticTokens) && record.semanticTokens.length > 0);
  assert.ok(Array.isArray(record.uniforms) && record.uniforms.length >= 3);
  const expectedUniformNames = configured.id === "nano-choreography"
    ? ["time", "width", "height", "render_pass"]
    : ["time", "width", "height"];
  assert.deepEqual(
    new Set(record.uniforms.map((uniform) => uniform.name)),
    new Set(expectedUniformNames)
  );
  const glsl = fs.readFileSync(path.join(projectDir, configured.fragment), "utf8");
  assert.match(glsl, /^#version 300 es/);
  assert.match(glsl, /void main/);
}

const terrainConfig = shaderConfig.scenes[1];
const terrainRecord = manifest.scenes[1];
assert.equal(terrainConfig.geometry.generator, "camera-lod-grid");
assert.equal(terrainRecord.shaders.vertex.path, terrainConfig.vertex.replace(/^web\//, ""));
assert.match(terrainRecord.shaders.vertex.hash, /^[a-f0-9]{64}$/);
assert.equal(terrainRecord.geometry.generator, terrainConfig.geometry.generator);
assert.equal(terrainRecord.geometry.path, undefined);
assert.equal(terrainRecord.geometry.format, "float32x4");
assert.equal(terrainRecord.geometry.attributeEncoding, "perspective-radial-ray-grid");
assert.equal(terrainRecord.geometry.primitive, "triangles");
assert.equal(terrainRecord.geometry.referenceWidthPixels, 1440);
assert.deepEqual(terrainRecord.geometry.render, {
  backgroundPass: false,
  blendMode: "opaque",
  depthTest: true
});
const terrainGeometry = resolveTerrainGeometryDescriptor(
  terrainRecord.geometry,
  terrainRecord.geometry.referenceWidthPixels
);
assert.strictEqual(
  resolveTerrainGeometryDescriptor(
    terrainRecord.geometry,
    terrainRecord.geometry.referenceWidthPixels
  ),
  terrainGeometry,
  "Identical generated geometry must be reused from the configuration-derived cache"
);
assert.equal(terrainGeometry.indices.format, "uint32");
assert.equal(
  terrainGeometry.indices.data.byteLength,
  terrainGeometry.indices.count * Uint32Array.BYTES_PER_ELEMENT
);
assert.equal(
  terrainGeometry.data.byteLength,
  terrainGeometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT
);
const terrainGeometryValues = new Float32Array(
  terrainGeometry.data
);
assert.deepEqual(
  [...terrainGeometryValues.slice(0, 12)],
  [-1, -1, 0, 0, 3, -1, 0, 0, -1, 3, 0, 0],
  "The terrain geometry must begin with its sky triangle"
);

function validateSampling(sampling, expectedRows, expectedNearColumns) {
  assert.deepEqual(
    Object.keys(sampling).sort(),
    ["farColumns", "nearColumns", "rows"]
  );
  assert.equal(sampling.rows, expectedRows);
  assert.equal(sampling.nearColumns, expectedNearColumns);
  assert.ok(Number.isInteger(sampling.farColumns) && sampling.farColumns > 0);
  assert.ok(sampling.nearColumns > sampling.farColumns);
}

function sampledColumns(sampling) {
  const decay = sampling.farColumns / sampling.nearColumns;
  return Array.from({ length: sampling.rows + 1 }, (_, row) => (
    row === sampling.rows
      ? sampling.farColumns
      : Math.round(sampling.nearColumns * decay ** (row / sampling.rows))
  ));
}

function configuredSurfaceVertexCount(sampling) {
  return sampledColumns(sampling).reduce((count, columns) => count + columns + 1, 0);
}

validateSampling(terrainConfig.geometry.terrainSampling, 200, 896);
validateSampling(terrainConfig.geometry.waterSampling, 104, 448);
assert.deepEqual(terrainConfig.geometry.cameraDistance, {
  near: 0.45,
  far: 377.8
});
assert.deepEqual(
  terrainRecord.geometry.cameraDistance,
  terrainConfig.geometry.cameraDistance
);
assert.deepEqual(
  terrainGeometry.surfaces.terrain.sampling,
  terrainConfig.geometry.terrainSampling
);
assert.deepEqual(
  terrainGeometry.surfaces.water.sampling,
  terrainConfig.geometry.waterSampling
);
assert.equal(terrainGeometry.surfaces.sky.value, 0);
assert.equal(terrainGeometry.surfaces.sky.vertexCount, 3);
assert.equal(terrainGeometry.surfaces.terrain.value, 1);
assert.equal(terrainGeometry.surfaces.water.value, 2);
assert.equal(
  terrainGeometry.surfaces.terrain.vertexCount,
  configuredSurfaceVertexCount(terrainConfig.geometry.terrainSampling)
);
assert.equal(
  terrainGeometry.surfaces.water.vertexCount,
  configuredSurfaceVertexCount(terrainConfig.geometry.waterSampling)
);
assert.equal(
  terrainGeometry.vertexCount,
  Object.values(terrainGeometry.surfaces)
    .reduce((count, surface) => count + surface.vertexCount, 0)
);
const wideTerrainGeometry = resolveTerrainGeometryDescriptor(
  terrainRecord.geometry,
  terrainRecord.geometry.referenceWidthPixels * 2
);
assert.deepEqual(wideTerrainGeometry.surfaces.terrain.sampling, {
  rows: terrainConfig.geometry.terrainSampling.rows * 2,
  nearColumns: terrainConfig.geometry.terrainSampling.nearColumns * 2,
  farColumns: terrainConfig.geometry.terrainSampling.farColumns * 2
});
assert.deepEqual(wideTerrainGeometry.surfaces.water.sampling, {
  rows: terrainConfig.geometry.waterSampling.rows * 2,
  nearColumns: terrainConfig.geometry.waterSampling.nearColumns * 2,
  farColumns: terrainConfig.geometry.waterSampling.farColumns * 2
});
assert.ok(
  wideTerrainGeometry.vertexCount > 65536,
  "Horizontal framebuffer scaling must support meshes beyond the 16-bit index range"
);
assert.notStrictEqual(
  resolveTerrainGeometryDescriptor({
    ...terrainRecord.geometry,
    terrainSampling: {
      ...terrainRecord.geometry.terrainSampling,
      nearColumns: terrainRecord.geometry.terrainSampling.nearColumns + 1
    }
  }, terrainRecord.geometry.referenceWidthPixels),
  terrainGeometry,
  "Changing any sampling input must invalidate the generated geometry cache"
);
const superwideWidth = 2560;
const superwideHeight = 900;
const superwideTerrainGeometry = resolveTerrainGeometryDescriptor(
  terrainRecord.geometry,
  superwideWidth
);
const mountainDistance = 50;
const superwideSampling = superwideTerrainGeometry.surfaces.terrain.sampling;
const mountainProgress = Math.log(
  mountainDistance / terrainRecord.geometry.cameraDistance.near
) / Math.log(
  terrainRecord.geometry.cameraDistance.far
    / terrainRecord.geometry.cameraDistance.near
);
const mountainColumns = Math.round(
  superwideSampling.nearColumns
    * (superwideSampling.farColumns / superwideSampling.nearColumns) ** mountainProgress
);
const longitudinalSpacing = mountainDistance * (
  (
    terrainRecord.geometry.cameraDistance.far
      / terrainRecord.geometry.cameraDistance.near
  ) ** (1 / superwideSampling.rows) - 1
);
const lateralSpacing = (
  2 * (superwideWidth / superwideHeight) * 0.8 * mountainDistance
) / mountainColumns;
assert.ok(
  longitudinalSpacing / lateralSpacing >= 0.75
    && longitudinalSpacing / lateralSpacing <= 1.5,
  "Superwide mountain triangles must have comparable longitudinal and lateral spacing"
);
const edgeRaySlope = (superwideWidth / superwideHeight) * 0.80;
const pitchedEdgeSlope = edgeRaySlope * Math.cos(0.155);
const edgeForwardSpacing = longitudinalSpacing / Math.hypot(1, pitchedEdgeSlope);
const edgeLateralAdvance = edgeForwardSpacing * pitchedEdgeSlope;
assert.ok(
  Math.abs(
    Math.hypot(edgeForwardSpacing, edgeLateralAdvance) - longitudinalSpacing
  ) < Number.EPSILON * longitudinalSpacing * 4,
  "Every perspective ray must advance the same world-space distance per row"
);

const uniformSurfaceVertexCount = (sampling) => (
  (sampling.rows + 1) * (sampling.nearColumns + 1)
);
const uniformVertexCount = 3
  + uniformSurfaceVertexCount(terrainConfig.geometry.terrainSampling)
  + uniformSurfaceVertexCount(terrainConfig.geometry.waterSampling);
assert.ok(
  terrainGeometry.vertexCount < uniformVertexCount * 0.65,
  "Distance sampling must eliminate at least 35% of uniform-grid vertices"
);

const surfaceRows = new Map();
for (let vertex = 3; vertex < terrainGeometry.vertexCount; vertex += 1) {
  const x = terrainGeometryValues[vertex * 4];
  const cameraDistance = terrainGeometryValues[vertex * 4 + 1];
  const surface = terrainGeometryValues[vertex * 4 + 3];
  assert.ok(x >= -1 && x <= 1);
  assert.ok(
    cameraDistance >= Math.fround(terrainConfig.geometry.cameraDistance.near)
      && cameraDistance <= Math.fround(terrainConfig.geometry.cameraDistance.far)
  );
  assert.equal(terrainGeometryValues[vertex * 4 + 2], 0);
  assert.ok(surface === 1 || surface === 2);
  const rowKey = `${surface}:${cameraDistance}`;
  surfaceRows.set(rowKey, (surfaceRows.get(rowKey) ?? 0) + 1);
}
for (const [surface, sampling] of [
  [1, terrainConfig.geometry.terrainSampling],
  [2, terrainConfig.geometry.waterSampling]
]) {
  const rowDensities = [...surfaceRows]
    .filter(([key]) => key.startsWith(`${surface}:`))
    .map(([key, count]) => ({ cameraDistance: Number(key.slice(2)), count }))
    .sort((left, right) => left.cameraDistance - right.cameraDistance);
  const expectedColumns = sampledColumns(sampling);
  const distanceRatio = (
    terrainConfig.geometry.cameraDistance.far
    / terrainConfig.geometry.cameraDistance.near
  );
  assert.equal(rowDensities.length, sampling.rows + 1);
  assert.deepEqual(
    rowDensities.map((row) => row.count - 1),
    expectedColumns,
    "Every fixed row must follow the configured exponential density"
  );
  for (let row = 0; row < rowDensities.length; row += 1) {
    const expectedDistance = row === sampling.rows
      ? terrainConfig.geometry.cameraDistance.far
      : terrainConfig.geometry.cameraDistance.near
        * distanceRatio ** (row / sampling.rows);
    assert.ok(
      Math.abs(rowDensities[row].cameraDistance - expectedDistance)
        < expectedDistance * 1e-6
    );
  }
}

const terrainIndices = new Uint32Array(terrainGeometry.indices.data);
assert.deepEqual([...terrainIndices.slice(0, 3)], [0, 1, 2]);
assert.ok([...terrainIndices].every((index) => index < terrainGeometry.vertexCount));

const surfaceByVertex = (vertex) => terrainGeometryValues[vertex * 4 + 3];
const edgeUseCounts = new Map();
for (let index = 0; index < terrainIndices.length; index += 3) {
  const triangle = [...terrainIndices.slice(index, index + 3)];
  const surface = surfaceByVertex(triangle[0]);
  assert.ok(triangle.every((vertex) => surfaceByVertex(vertex) === surface));
  if (surface === 0) continue;
  for (const [left, right] of [[triangle[0], triangle[1]], [triangle[1], triangle[2]], [triangle[2], triangle[0]]]) {
    const edge = left < right ? `${left}:${right}` : `${right}:${left}`;
    edgeUseCounts.set(edge, (edgeUseCounts.get(edge) ?? 0) + 1);
  }
}
for (const [edge, useCount] of edgeUseCounts) {
  assert.ok(useCount === 1 || useCount === 2);
  if (useCount === 2) continue;
  const [left, right] = edge.split(":").map(Number);
  const leftX = terrainGeometryValues[left * 4];
  const rightX = terrainGeometryValues[right * 4];
  const leftDistance = terrainGeometryValues[left * 4 + 1];
  const rightDistance = terrainGeometryValues[right * 4 + 1];
  const nearDistance = Math.fround(terrainConfig.geometry.cameraDistance.near);
  const farDistance = Math.fround(terrainConfig.geometry.cameraDistance.far);
  const boundaryEdge = (leftX === -1 && rightX === -1)
    || (leftX === 1 && rightX === 1)
    || (leftDistance === nearDistance && rightDistance === nearDistance)
    || (leftDistance === farDistance && rightDistance === farDistance);
  assert.equal(boundaryEdge, true, "LOD transitions must not leave interior cracks");
}

const nanoConfig = shaderConfig.scenes[2];
const nanoRecord = manifest.scenes[2];
assert.ok(nanoConfig.geometry, "Nano choreography must configure volumetric geometry");
assert.equal(nanoRecord.shaders.vertex.path, nanoConfig.vertex.replace(/^web\//, ""));
assert.match(nanoRecord.shaders.vertex.hash, /^[a-f0-9]{64}$/);
assert.equal(nanoRecord.geometry.path, nanoConfig.geometry.path.replace(/^web\//, ""));
assert.equal(nanoRecord.geometry.format, "float32x4");
assert.equal(nanoRecord.geometry.attributeEncoding, "paired-unorm12-wheel-corner");
assert.equal(nanoRecord.geometry.primitive, "triangles");
assert.ok(nanoRecord.geometry.pointCount >= 30000);
assert.equal(nanoRecord.geometry.vertexCount, nanoRecord.geometry.pointCount * 3);
assert.deepEqual(nanoRecord.geometry.render, {
  backgroundPass: true,
  blendMode: "additive",
  depthTest: false
});
assert.match(nanoRecord.geometry.hash, /^[a-f0-9]{64}$/);
const geometryBytes = fs.readFileSync(path.join(projectDir, nanoConfig.geometry.path));
assert.equal(geometryBytes.byteLength, nanoRecord.geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT);
const geometryValues = new Float32Array(
  geometryBytes.buffer,
  geometryBytes.byteOffset,
  geometryBytes.byteLength / Float32Array.BYTES_PER_ELEMENT
);
const targetPoints = [];
const motorcyclePoints = [];
const motorcycleWheelPoints = [];
for (let vertex = 0; vertex < nanoRecord.geometry.vertexCount; vertex += 1) {
  for (let axis = 0; axis < 3; axis += 1) {
    const packedCoordinate = geometryValues[vertex * 4 + axis];
    assert.ok(
      Number.isInteger(packedCoordinate)
        && packedCoordinate >= 0
        && packedCoordinate <= 16777215,
      "Each geometry axis must exactly pack one motorcycle and one Vitruvian coordinate"
    );
  }
  if (vertex % 3 === 0) {
    const target = [];
    const motorcycle = [];
    for (let axis = 0; axis < 3; axis += 1) {
      const packedCoordinate = geometryValues[vertex * 4 + axis];
      const targetQuantized = Math.floor(packedCoordinate / 4096);
      const motorcycleQuantized = packedCoordinate - targetQuantized * 4096;
      target.push((targetQuantized / 4095) * 4 - 2);
      motorcycle.push((motorcycleQuantized / 4095) * 4 - 2);
    }
    targetPoints.push(target);
    motorcyclePoints.push(motorcycle);
  }
  const encodedCorner = geometryValues[vertex * 4 + 3];
  const wheelPoint = Math.floor(encodedCorner / 4);
  assert.equal(
    encodedCorner - wheelPoint * 4,
    vertex % 3,
    "Geometry must preserve the micro-triangle corner"
  );
  assert.ok(wheelPoint === 0 || wheelPoint === 1);
  if (vertex % 3 === 0) motorcycleWheelPoints.push(wheelPoint);
}
assert.ok(motorcycleWheelPoints.some((wheelPoint) => wheelPoint === 0));
assert.ok(motorcycleWheelPoints.filter((wheelPoint) => wheelPoint === 1).length > 30000);
for (let point = 0; point < motorcyclePoints.length; point += 1) {
  if (motorcycleWheelPoints[point] === 0) continue;
  const [x, y, z] = motorcyclePoints[point];
  const centerX = x < 0 ? -0.72 : 0.72;
  assert.ok(Math.hypot(x - centerX, y + 0.42) < 0.38);
  assert.ok(Math.abs(z) < 0.15);
}

function coordinateCorrelation(firstPoints, secondPoints, axis) {
  const count = firstPoints.length;
  const firstMean = firstPoints.reduce((sum, point) => sum + point[axis], 0) / count;
  const secondMean = secondPoints.reduce((sum, point) => sum + point[axis], 0) / count;
  let covariance = 0;
  let firstVariance = 0;
  let secondVariance = 0;
  for (let index = 0; index < count; index += 1) {
    const firstDelta = firstPoints[index][axis] - firstMean;
    const secondDelta = secondPoints[index][axis] - secondMean;
    covariance += firstDelta * secondDelta;
    firstVariance += firstDelta * firstDelta;
    secondVariance += secondDelta * secondDelta;
  }
  return covariance / Math.sqrt(firstVariance * secondVariance);
}

const horizontalCorrelation = coordinateCorrelation(targetPoints, motorcyclePoints, 0);
const verticalCorrelation = coordinateCorrelation(targetPoints, motorcyclePoints, 1);
const planarRootMeanSquareTravel = Math.sqrt(
  targetPoints.reduce((sum, target, index) => (
    sum
      + (target[0] - motorcyclePoints[index][0]) ** 2
      + (target[1] - motorcyclePoints[index][1]) ** 2
  ), 0) / targetPoints.length
);
assert.ok(
  horizontalCorrelation > 0.7 && verticalCorrelation > 0.7,
  "Point correspondence must preserve neighboring horizontal and vertical regions"
);
assert.ok(
  planarRootMeanSquareTravel < 0.6,
  "Point correspondence must form coherent flights instead of a random dissolving cloud"
);
const geometryMetadata = JSON.parse(
  fs.readFileSync(path.join(projectDir, nanoConfig.geometry.metadata), "utf8")
);
assert.equal(geometryMetadata.schemaVersion, 5);
assert.equal(geometryMetadata.attributeEncoding, "paired-unorm12-wheel-corner");
assert.deepEqual(geometryMetadata.coordinateEncoding, {
  name: "paired-unorm12",
  quantizationLevels: 4095,
  coordinateMinimum: -2,
  coordinateMaximum: 2
});
assert.deepEqual(geometryMetadata.triangleCornerEncoding, {
  name: "wheel-part-plus-corner",
  wheelOffset: 4,
  cornerCount: 3
});
assert.deepEqual(geometryMetadata.pointPairing, {
  name: "recursive-spatial-bisection",
  leafPointCount: 64,
  axisOrder: ["x", "y", "z"]
});
assert.equal(geometryMetadata.source.uid, "6c0b99ce8463468fbd00f304dbe7e105");
assert.equal(geometryMetadata.source.title, "The Vitruvian Man");
assert.equal(geometryMetadata.source.author, "Fri");
assert.equal(geometryMetadata.source.license, "CC-BY-4.0");
assert.equal(
  geometryMetadata.source.url,
  "https://sketchfab.com/3d-models/the-vitruvian-man-6c0b99ce8463468fbd00f304dbe7e105"
);
assert.match(geometryMetadata.source.archiveSha256, /^[a-f0-9]{64}$/);
assert.match(geometryMetadata.source.meshSha256, /^[a-f0-9]{64}$/);
assert.equal(geometryMetadata.source.modelVertexCount, 241794);
assert.equal(geometryMetadata.source.modelTriangleCount, 483637);
assert.ok(Array.isArray(geometryMetadata.modifications));
assert.equal(geometryMetadata.pointCount, nanoRecord.geometry.pointCount);
assert.equal(geometryMetadata.motorcyclePointCount, geometryMetadata.pointCount);
assert.equal(
  geometryMetadata.motorcycleWheelPointCount,
  motorcycleWheelPoints.filter((wheelPoint) => wheelPoint === 1).length
);
assert.ok(geometryMetadata.surfacePointCount >= 60000);
assert.ok(geometryMetadata.densityPointCount >= 5000);
assert.equal("detailPointCount" in geometryMetadata, false);
assert.equal("alternateArmPointCount" in geometryMetadata, false);
assert.equal("alternateLegPointCount" in geometryMetadata, false);
assert.equal(nanoRecord.geometry.attribution.title, geometryMetadata.source.title);
assert.equal(nanoRecord.geometry.attribution.author, geometryMetadata.source.author);
assert.equal(nanoRecord.geometry.attribution.license, geometryMetadata.source.license);

const pointCloudGenerator = fs.readFileSync(
  path.join(projectDir, "tools/homepage-shaders/generate-point-cloud.py"),
  "utf8"
);
assert.match(pointCloudGenerator, /6c0b99ce8463468fbd00f304dbe7e105/);
assert.match(pointCloudGenerator, /bpy\.ops\.wm\.stl_import/);
for (const pairedGeometryOperation of [
  "motorcycle_points",
  "sample_torus",
  "sample_ellipsoid",
  "sample_segment_tube",
  "spatially_pair_points",
  "pack_coordinate_pair"
]) {
  assert.match(
    pointCloudGenerator,
    new RegExp(`def ${pairedGeometryOperation}\\(`),
    `The geometry generator must define ${pairedGeometryOperation.replaceAll("_", " ")}`
  );
}
assert.doesNotMatch(
  pointCloudGenerator,
  /limb_weights|alternate_arms|alternate_legs|append_frame|BODY_OBJECT|EYE_OBJECTS|DETAIL_POINT_COUNT|detail_sampler|detail_points|predicate/,
  "The selected Vitruvian model must provide its own anatomy with uniform sampling"
);

const shaderReadme = fs.readFileSync(
  path.join(projectDir, "tools/homepage-shaders/README.md"),
  "utf8"
);
assert.match(shaderReadme, /--stl/);
assert.doesNotMatch(shaderReadme, /--blend/);

const attributionPage = fs.readFileSync(
  path.join(projectDir, "web/wiki/attributions.html"),
  "utf8"
);
assert.match(attributionPage, /The Vitruvian Man/);
assert.match(attributionPage, />Fri</);
assert.match(attributionPage, /CC BY 4\.0/);
assert.match(attributionPage, /6c0b99ce8463468fbd00f304dbe7e105/);

const html = fs.readFileSync(path.join(projectDir, "web/index.html"), "utf8");
assert.match(html, /<section[^>]+data-live-shader-banner/);
assert.ok(
  html.indexOf("data-live-shader-banner") < html.indexOf("hero-workbench"),
  "The live shader must be the first homepage banner"
);
assert.equal(
  (html.match(/<canvas[^>]+data-shader-canvas/g) ?? []).length,
  2,
  "Two alternating full-resolution canvases must overlap consecutive shader thoughts"
);
assert.equal((html.match(/data-shader-layer=/g) ?? []).length, 2);
assert.equal((html.match(/data-shader-canvas="immersive"/g) ?? []).length, 2);
assert.doesNotMatch(html, /data-shader-canvas="thought"/);
assert.match(html, /data-shader-code/);
assert.match(html, /data-shader-editor-link/);
assert.match(html, /data-shader-next/);
assert.match(html, /class="thought-assembly"/);
assert.doesNotMatch(html, /thought-cloud-guide|class="thought-cloud"|thought-cloud-shape/);
assert.equal((html.match(/data-thought-cloud-path=/g) ?? []).length, 2);
assert.equal((html.match(/class="thought-tail-dot/g) ?? []).length, 3);
assert.match(html, /data-thought-origin-dot/);
assert.match(html, /data-thought-cloud-dot/);
assert.doesNotMatch(html, /<video\b|data-shader-film/);

const homepageJavascript = fs.readFileSync(path.join(projectDir, "web/homepage.js"), "utf8");
const shaderBannerJavascript = fs.readFileSync(path.join(projectDir, "web/shader-banner.js"), "utf8");
const sharedRenderer = fs.readFileSync(path.join(projectDir, "web/shader-renderer.js"), "utf8");
const ideMain = fs.readFileSync(path.join(projectDir, "src/web/ide/src/main.js"), "utf8");
assert.match(homepageJavascript, /createShaderBanner/);
assert.match(shaderBannerJavascript, /shaders\/manifest\.json/);
assert.match(shaderBannerJavascript, /renderSemanticTokens/);
assert.match(shaderBannerJavascript, /prefers-reduced-motion/);
assert.match(shaderBannerJavascript, /data-shader-editor-link/);
assert.match(shaderBannerJavascript, /scene:\s*scene\.id/);
assert.doesNotMatch(shaderBannerJavascript, /code64|renderer64/);
assert.match(shaderBannerJavascript, /mode:\s*"shader"/);
assert.match(shaderBannerJavascript, /prepareIncomingScene/);
assert.match(shaderBannerJavascript, /promoteIncomingScene/);
assert.match(shaderBannerJavascript, /preloadNextScene/);
assert.match(shaderBannerJavascript, /preloadedShaderIndex/);
assert.match(shaderBannerJavascript, /updateThoughtAssembly/);
assert.match(shaderBannerJavascript, /updateThoughtTail/);
assert.match(shaderBannerJavascript, /visibleCloudGeometry/);
assert.match(shaderBannerJavascript, /INCOMING_CODE_START/);
assert.match(shaderBannerJavascript, /layer\.path\.setAttribute\("transform"/);
assert.match(shaderBannerJavascript, /translate\(-1 -1\) scale\(3 3\)/);
assert.doesNotMatch(shaderBannerJavascript, /initialEntrance/);
assert.doesNotMatch(shaderBannerJavascript, /transitionEndsAt/);
assert.doesNotMatch(shaderBannerJavascript, /incomingLayer\.startedAt\s*=\s*timestamp/);
assert.match(
  shaderBannerJavascript,
  /sceneStartedAt\s*=\s*timestamp;\s*updateActiveReadout/,
  "The promoted shader must receive its complete full-screen banner interval"
);
assert.doesNotMatch(shaderBannerJavascript, /thoughtPreview/);
assert.doesNotMatch(shaderBannerJavascript, /HTMLMediaElement|\.play\(|\.pause\(|webm|mp4|poster/);
assert.match(sharedRenderer, /getContext\("webgl2"/);
assert.match(sharedRenderer, /window\.devicePixelRatio \|\| 1/);
assert.doesNotMatch(sharedRenderer, /Math\.min\(window\.devicePixelRatio/);
assert.match(sharedRenderer, /gl\.vertexAttribPointer/);
assert.match(sharedRenderer, /gl\.drawArrays\(gl\.TRIANGLES/);
assert.match(sharedRenderer, /gl\.drawElements\(gl\.TRIANGLES/);
assert.match(sharedRenderer, /geometry\.vertexCount/);
assert.match(sharedRenderer, /depth:\s*true/);
assert.match(sharedRenderer, /gl\.DEPTH_BUFFER_BIT/);
assert.match(sharedRenderer, /pass\.depthTest/);
assert.doesNotMatch(sharedRenderer, /paired-unorm12/);
assert.match(ideMain, /from "\.\.\/\.\.\/\.\.\/\.\.\/web\/shader-renderer\.js"/);
assert.match(ideMain, /shaders\/manifest\.json/);
assert.doesNotMatch(ideMain, /renderer64/);

const styles = [
  fs.readFileSync(path.join(projectDir, "web/style.css"), "utf8"),
  fs.readFileSync(path.join(projectDir, "web/shader-banner.css"), "utf8")
].join("\n");
assert.match(styles, /\.live-shader-section/);
assert.match(styles, /\.thought-assembly/);
assert.doesNotMatch(styles, /thought-cloud-guide|\.thought-cloud|--thought-guide-opacity/);
assert.match(styles, /\.thought-tail-dot/);
assert.match(styles, /\[data-layer-state="revealing"\]/);
assert.match(styles, /--laptop-code-opacity/);
assert.match(styles, /\.site-header/);
assert.doesNotMatch(styles, /\.shader-film|\.film-grain|@keyframes film-grain/);

const buildScript = fs.readFileSync(path.join(projectDir, "scripts/build_web_root.sh"), "utf8");
assert.match(buildScript, /tools\/homepage-shaders\/generate\.mjs/);
assert.doesNotMatch(buildScript, /homepage-video|render_homepage_video/);

for (const removedPath of [
  "tools/homepage-video",
  "scripts/render_homepage_video.sh",
  "web/media/code-to-shader.json"
]) {
  assert.equal(fs.existsSync(path.join(projectDir, removedPath)), false, `Obsolete path remains: ${removedPath}`);
}
const obsoleteMedia = fs.existsSync(path.join(projectDir, "web/media"))
  ? fs.readdirSync(path.join(projectDir, "web/media")).filter((name) => /\.(?:mp4|webm|webp)$/i.test(name))
  : [];
assert.deepEqual(obsoleteMedia, [], "Encoded homepage media must be removed");

const checkStartedAt = performance.now();
const checkResult = spawnSync(process.execPath, [path.join(toolDir, "generate.mjs"), "--check"], {
  cwd: projectDir,
  encoding: "utf8"
});
const checkMilliseconds = performance.now() - checkStartedAt;
assert.equal(
  checkResult.status,
  0,
  `Generated homepage shaders are stale or invalid:\n${checkResult.stdout}${checkResult.stderr}`
);
assert.ok(checkMilliseconds < 30000, `Live shader generation took ${checkMilliseconds.toFixed(0)} ms`);

console.log(`Homepage live shaders are valid (${checkMilliseconds.toFixed(0)} ms verification).`);
