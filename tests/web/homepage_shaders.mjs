import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(testDir, "../..");
const toolDir = path.join(projectDir, "tools/homepage-shaders");
const expectedToolFiles = ["README.md", "compiler.mjs", "config.mjs", "generate.mjs", "generate-point-cloud.py"];

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
const compilerSource = fs.readFileSync(path.join(toolDir, "compiler.mjs"), "utf8");
assert.match(compilerSource, /const semanticTokensBySource = new Map\(\)/);
assert.match(compilerSource, /semanticTokensBySource\.get\(source\)/);
for (const [index, source] of shaderSources.entries()) {
  const sourceWithoutImports = source
    .split("\n")
    .filter((line) => !line.startsWith("import "))
    .join("\n");
  assert.doesNotMatch(
    source,
    /\b(?:cell_x|cell_y|grid_x|grid_y|value noise|fractal noise)\b/i,
    `${shaderConfig.scenes[index].id} must not reconstruct a visible square lattice`
  );
  assert.doesNotMatch(
    sourceWithoutImports,
    /\b[a-z][a-z0-9]*_[a-z0-9_]+\b/,
    `${shaderConfig.scenes[index].id} must use plain-English identifiers`
  );
}
assert.doesNotMatch(sharedSource, /function (?:value|fractal) noise\b/i);
assert.doesNotMatch(sharedSource, /\bcell_[xy]\b/i);
assert.match(
  sharedSource,
  /function the simplex field at \{a point:point\} during \{a value:phase\}/,
  "Shared shader art must provide a non-square procedural field"
);

function withoutAssignmentGrouping(source) {
  return source.split("\n").map((line) => {
    const match = line.match(/^(\s*set .+? to )\((.*)\)$/);
    if (!match) return line;
    let depth = 1;
    let quoted = false;
    for (let index = 0; index < match[2].length; index += 1) {
      const character = match[2][index];
      if (character === "\"" && match[2][index - 1] !== "\\") quoted = !quoted;
      if (quoted) continue;
      if (character === "(") depth += 1;
      if (character === ")") depth -= 1;
      if (depth === 0) return line;
    }
    return depth === 1 ? match[1] + match[2] : line;
  }).join("\n");
}

const terrainSource = withoutAssignmentGrouping(shaderSources[1]);
assert.match(terrainSource, /function the terrain height at \{terrain coordinate:position\}/);
assert.match(terrainSource, /position's x \* 0\.011/);
assert.match(terrainSource, /position's z \* 0\.011/);
assert.match(terrainSource, /set crest /);
assert.match(terrainSource, /set ridge /);
assert.match(terrainSource, /set erosion /);
assert.match(terrainSource, /if this is a vertex shader/);
assert.match(terrainSource, /set the shader interpolant named "terrain position"/);
assert.match(terrainSource, /set the shader interpolant named "terrain normal"/);
assert.match(terrainSource, /set the shader interpolant named "terrain material"/);
assert.match(terrainSource, /the shader interpolant [xyzw] named "terrain position"/);
assert.match(terrainSource, /the shader interpolant [xyzw] named "terrain normal"/);
assert.match(terrainSource, /the shader interpolant [xyz] named "terrain material"/);
assert.match(
  terrainSource,
  /function the maximum possible terrain height:\s+execute:\s+return 12\.10/
);
assert.match(
  terrainSource,
  /function the terrain camera at moment:[\s\S]*y \(\(the maximum possible terrain height\) \+ 0\.70\)/
);
assert.equal(
  (terrainSource.match(/set camera to the terrain camera at time/g) ?? []).length,
  2,
  "Vertex and fragment water stages must share the fixed-altitude camera"
);
assert.match(terrainSource, /set pitch to 0\.155/);
assert.match(terrainSource, /set vertical to 0\.0 - camera's y/);
assert.match(terrainSource, /set slope to \(\(the vertex x\) \* aspect\) \* 0\.80/);
assert.match(
  terrainSource,
  /set scale to the square root of \(1\.0 \+ \(\(slope \* cosine\) \* \(slope \* cosine\)\)\)/
);
assert.match(terrainSource, /set forward to distance \/ scale/);
assert.match(terrainSource, /set depth to \(forward \* cosine\) - \(vertical \* sine\)/);
assert.match(terrainSource, /set lateral to slope \* depth/);
assert.match(
  terrainSource,
  /set the shader interpolant named "terrain normal" with an x coordinate of normal's x, a y coordinate of normal's y, a z coordinate of normal's z and a w coordinate of distance/
);
assert.match(
  terrainSource,
  /set clip to a spatial coordinate with x \(lateral \/ \(aspect \* 0\.72\)\), y \(view's x \/ 0\.72\) and z \(\(view's y \* 1\.00078\) - 0\.40016\)/
);
assert.match(
  terrainSource,
  /function the water detail visibility at distance:\s+execute:\s+return 1\.0 - the smooth transition from 48\.0 to 96\.0 at distance/
);
assert.match(terrainSource, /set stride to 0\.34/);
assert.match(
  terrainSource,
  /set west to the terrain height at \(a terrain coordinate with x \(world's x - stride\) and z world's z\)/
);
assert.match(
  terrainSource,
  /set east to the terrain height at \(a terrain coordinate with x \(world's x \+ stride\) and z world's z\)/
);
assert.match(
  terrainSource,
  /set south to the terrain height at \(a terrain coordinate with x world's x and z \(world's z - stride\)\)/
);
assert.match(
  terrainSource,
  /set north to the terrain height at \(a terrain coordinate with x world's x and z \(world's z \+ stride\)\)/
);
assert.match(terrainSource, /set the x coordinate of normal to west - east/);
assert.match(terrainSource, /set the y coordinate of normal to stride \* 2\.0/);
assert.match(terrainSource, /set the z coordinate of normal to south - north/);
assert.doesNotMatch(terrainSource, /set stride to .*\bdistance\b/,
  "Terrain normals must use one centered world-space gradient at every LOD");
assert.doesNotMatch(terrainSource, /\bstrata\b/,
  "Mountain materials must not paint contour bands over the smooth terrain normals");
assert.match(terrainSource, /set exposure /);
assert.match(terrainSource, /set snow /);
assert.match(terrainSource, /if surface > 1\.5/);
assert.match(terrainSource, /set level to -0\.62/);
assert.match(terrainSource, /set visibility to the water detail visibility at distance/);
assert.match(
  terrainSource,
  /set variation to 0\.5 \+ \(\(\(wave's x \+ wave's y\) \* 0\.25\) \* visibility\)/
);
assert.match(terrainSource, /set detail to 0\.5 \+ \(\(wave's z \* 0\.5\) \* visibility\)/);
assert.match(
  terrainSource,
  /set submersion to the maximum of \(level - the terrain height at world\) and 0\.0/
);
assert.match(
  terrainSource,
  /set shallows to \(1\.0 - the smooth transition from 0\.18 to 3\.8 at material's z\) \* visibility/
);
assert.match(
  terrainSource,
  /set depth to \(1\.0 - the smooth transition from 0\.05 to 0\.65 at material's z\) \* visibility/
);
assert.match(terrainSource, /set caustic /);
assert.match(terrainSource, /set facing /);
assert.match(terrainSource, /set grazing to 1\.0 - facing/);
assert.match(terrainSource, /set fresnel to 0\.020 \+/);
assert.match(terrainSource, /set altitude /);
assert.match(
  terrainSource,
  /set alignment to \(\(\(reflection's x \* 0\.39\) \+ \(reflection's y \* 0\.32\)\) \+ \(reflection's z \* 0\.86\)\) saturated/
);
assert.match(terrainSource, /set glow to glint/);
assert.match(terrainSource, /set glint to glint \* \(0\.35 \+ \(fresnel \* 0\.65\)\)/);
assert.match(terrainSource, /simplex field at/);
assert.doesNotMatch(terrainSource, /\b(?:signed flow|ridged field) at\b/,
  "Terrain must not be assembled from periodic wave ridges");
assert.doesNotMatch(terrainSource, /\b(?:terrain hit|hit distance|refinement)\b/);
assert.doesNotMatch(terrainSource, /\briver\b/i);

const nanoSource = withoutAssignmentGrouping(shaderSources[2]);
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
  /\bcrystal\b/i,
  "The nano sequence must move directly between the motorcycle and Vitruvian figure"
);
for (const threeDimensionalDetail of [
  "set packed to a spatial coordinate",
  "set target to a spatial coordinate",
  "set quantized to a spatial coordinate",
  "set point to a spatial coordinate",
  "set local to a spatial coordinate",
  "set turned to a spatial coordinate",
  "set world to a spatial coordinate",
  "set motorcycle to a planar coordinate",
  "set projection to a planar coordinate",
  "this is a vertex shader",
  "shader render pass",
  "persistent point population"
]) {
  assert.ok(
    nanoSource.includes(threeDimensionalDetail),
    `Nano choreography must define ${threeDimensionalDetail}`
  );
}
assert.match(
  nanoSource,
  /set size to 0\.00104 \* \(0\.96 \+ \(pass \* 0\.04\)\)/,
  "Volumetric points must keep a resolution-independent physical footprint"
);
assert.match(
  nanoSource,
  /set extent to a planar coordinate with x \(size \* scale\) and y size/,
  "Point width must compensate for the viewport aspect ratio"
);
assert.doesNotMatch(
  nanoSource,
  /motorcycle distance|ray march|surface drones|surface hit|hologram glow|motorcycle part/,
  "The motorcycle must come from paired geometry points, not a raymarch or runtime geometry generator"
);
assert.match(
  nanoSource,
  /set color to a radiant color with red \(\(0\.08 \+ \(wave \* 0\.28\)\) \+ \(warmth \* 0\.38\)\), green \(\(0\.30 \+ \(scan \* 0\.24\)\) \+ \(warmth \* 0\.12\)\) and blue \(\(0\.66 \+ \(wave \* 0\.30\)\) - \(warmth \* 0\.18\)\)/,
  "Vitruvian points must remain visibly blue-green"
);
assert.match(
  nanoSource,
  /set size to 0\.00104 \* \(0\.96 \+ \(pass \* 0\.04\)\)/,
  "Every volumetric point must use the same viewport-relative footprint"
);
assert.match(
  nanoSource,
  /set extent to a planar coordinate with x \(size \* scale\) and y size/,
  "Point width must compensate for viewport aspect ratio"
);
assert.doesNotMatch(
  nanoSource,
  /set size to [^\n]*\/ frame's x/,
  "Point size must not remain fixed in physical framebuffer pixels"
);
assert.match(
  nanoSource,
  /set triangle to encoding - \(wheel \* 4\.0\)/,
  "Geometry must decode the wheel flag without changing the triangle corner"
);
assert.doesNotMatch(
  nanoSource,
  /\b(?:point group|build wave)\b/,
  "The figure must not use region-specific weights or a build-wave visibility mask"
);
assert.match(
  nanoSource,
  /set assembly to the smooth transition from \(4\.45 \+ delay\) to \(6\.65 \+ delay\) at moment/,
  "Each drone must receive a long, stable, staggered flight window"
);
assert.match(
  nanoSource,
  /set assembled to a planar coordinate with x \(\(motorcycle's x \+ \(\(projection's x - motorcycle's x\) \* assembly\)\) \+ flight's x\) and y \(\(motorcycle's y \+ \(\(projection's y - motorcycle's y\) \* assembly\)\) \+ flight's y\)/,
  "The same points must fly directly from motorcycle positions into their final model positions"
);
assert.match(
  nanoSource,
  /set arc to \(the sine of \(assembly \* 3\.14159265\)\) \* \(0\.10 \+ \(variation \* 0\.24\)\)/,
  "Curved flight must converge exactly at both endpoint shapes"
);
assert.match(
  nanoSource,
  /set seed to \(\(the sine of \(\(\(point's x \* 3\.13\) \+ \(point's y \* 2\.71\)\) \+ \(point's z \* 4\.19\)\)\) \* 0\.5\) \+ 0\.5/,
  "Neighboring drones must follow a continuous flock field instead of random-looking paths"
);
assert.doesNotMatch(
  nanoSource,
  /\bswarm\b/,
  "The transition must not pass through an unrelated synthetic cloud"
);
assert.match(
  nanoSource,
  /set projection to a planar coordinate with x \(\(\(turned's x \* 1\.66\) \* scale\) \/ destination\) and y \(\(\(point's y \* 1\.86\) - 0\.04\) \/ destination\)/,
  "The Vitruvian target must remain centered in its perspective projection"
);
assert.match(
  nanoSource,
  /set yaw to 1\.30 \+ \(progress \* 0\.04\)/,
  "The motorcycle front must lead its movement toward the camera"
);
assert.match(
  nanoSource,
  /set motorcycle to a planar coordinate with x \(\(\(world's x \* 1\.72\) \* scale\) \/ depth\) and y \(\(world's y \* 1\.72\) \/ depth\)/,
  "The motorcycle must use a perspective divide after its world transform"
);
assert.match(nanoSource, /set scale to 1\.0 \/ aspect/);
assert.match(
  nanoSource,
  /set flight to a planar coordinate with x \(\(\(\(the cosine of phase\) \* arc\) \+ sweep\) \* scale\) and y \(\(\(the sine of phase\) \* arc\) \* 0\.72\)/
);
assert.doesNotMatch(nanoSource, /the maximum of aspect and 1\.0/, "Portrait projection must not squash the drone scene horizontally");
assert.match(
  nanoSource,
  /set clip to a planar coordinate with x \(\(assembled's x \+ corner's x\) \* depth\) and y \(\(assembled's y \+ corner's y\) \* depth\)/,
  "Projected motorcycle points must return to homogeneous clip space"
);
assert.match(
  nanoSource,
  /set distance to the distance from screen to segment from center to ending/,
  "The measurement line must extend from the model center to the circle"
);
assert.match(
  nanoSource,
  /set ending to a planar coordinate with x \(\(turned's x \* 1\.66\) \/ depth\) and y \(\(\(source's y \* 1\.86\) - 0\.04\) \/ depth\)/,
  "The radius endpoint must use the same perspective projection as the circle"
);
assert.match(
  nanoSource,
  /set visibility to the smooth transition from 7\.55 to 8\.10 at moment/,
  "The measurement must remain visible for the completed one-shot composition"
);
assert.match(
  nanoSource,
  /set angle to \(time - 7\.55\) \* 1\.32/,
  "The measurement must keep rotating after the one-shot choreography clock stops"
);
assert.doesNotMatch(
  nanoSource,
  /set angle to \(moment -/,
  "The measurement angle must not use the clamped choreography clock"
);
assert.doesNotMatch(
  nanoSource,
  /function vitruvian distance at/,
  "The model-derived Vitruvian point cloud must replace the analytic body approximation"
);
assert.doesNotMatch(
  nanoSource,
  /distance from point bike|distance from point human/,
  "The motorcycle and Vitruvian figure must not be 2D line drawings"
);
for (const sharedThreeDimensionalPrimitive of [
  "function the distance from {a point:point} to capsule from {a point:start} to {a point:end} with radius",
  "function the ellipsoid distance from {a point:point} with radii {a point:radii}",
  "function the torus distance from {a point:point} with major radius"
]) {
  assert.ok(
    sharedSource.includes(sharedThreeDimensionalPrimitive),
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
