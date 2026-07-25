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

const terrainSource = shaderSources[1];
assert.match(terrainSource, /function terrain height at x z/);
assert.match(terrainSource, /while march_step < \d+ and not terrain_hit/);
assert.match(terrainSource, /set surface_height to terrain height at sample_x sample_z/);
assert.match(terrainSource, /set ridge_three /);
assert.match(terrainSource, /set alpine_peaks /);
assert.match(terrainSource, /set ground_detail /);
assert.doesNotMatch(terrainSource, /\briver\b/i);

const nanoSource = shaderSources[2];
for (const threeDimensionalDetail of [
  "motorcycle distance at x y z",
  "camera_ray_x",
  "camera_ray_y",
  "camera_ray_z",
  "sample_z",
  "motorcycle_yaw",
  "wheel_depth",
  "this is a vertex shader",
  "shader render pass",
  "volumetric point"
]) {
  assert.match(
    nanoSource,
    new RegExp(threeDimensionalDetail.replaceAll(" ", "\\s+")),
    `Nano choreography must define ${threeDimensionalDetail.replaceAll("_", " ")}`
  );
}
assert.match(nanoSource, /while ray_step < \d+ and not ray_finished/);
assert.match(
  nanoSource,
  /set point_size to .* \/ width/,
  "Volumetric points must keep a resolution-independent physical footprint"
);
assert.match(
  nanoSource,
  /set figure_offset_x to .*smooth transition/,
  "The Vitruvian composition must move clear of the headline on wide viewports"
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
assert.equal(manifest.schemaVersion, 2);
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
  assert.deepEqual(
    new Set(record.uniforms.map((uniform) => uniform.name)),
    new Set(["time", "width", "height", ...(configured.geometry ? ["render_pass"] : [])])
  );
  const glsl = fs.readFileSync(path.join(projectDir, configured.fragment), "utf8");
  assert.match(glsl, /^#version 300 es/);
  assert.match(glsl, /void main/);
}

const nanoConfig = shaderConfig.scenes[2];
const nanoRecord = manifest.scenes[2];
assert.ok(nanoConfig.geometry, "Nano choreography must configure volumetric geometry");
assert.equal(nanoRecord.shaders.vertex.path, nanoConfig.vertex.replace(/^web\//, ""));
assert.match(nanoRecord.shaders.vertex.hash, /^[a-f0-9]{64}$/);
assert.equal(nanoRecord.geometry.path, nanoConfig.geometry.path.replace(/^web\//, ""));
assert.equal(nanoRecord.geometry.format, "float32x4");
assert.equal(nanoRecord.geometry.primitive, "triangles");
assert.ok(nanoRecord.geometry.pointCount >= 30000);
assert.equal(nanoRecord.geometry.vertexCount, nanoRecord.geometry.pointCount * 3);
assert.match(nanoRecord.geometry.hash, /^[a-f0-9]{64}$/);
const geometryBytes = fs.readFileSync(path.join(projectDir, nanoConfig.geometry.path));
assert.equal(geometryBytes.byteLength, nanoRecord.geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT);
const geometryMetadata = JSON.parse(
  fs.readFileSync(path.join(projectDir, nanoConfig.geometry.metadata), "utf8")
);
assert.equal(geometryMetadata.schemaVersion, 1);
assert.equal(geometryMetadata.license, "CC0-1.0");
assert.match(geometryMetadata.source.sha256, /^[a-f0-9]{64}$/);
assert.equal(geometryMetadata.pointCount, nanoRecord.geometry.pointCount);
assert.ok(geometryMetadata.surfacePointCount >= 20000);
assert.ok(geometryMetadata.interiorPointCount >= 5000);
assert.ok(geometryMetadata.headPointCount >= 5000);

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
assert.match(shaderBannerJavascript, /code64/);
assert.match(shaderBannerJavascript, /mode:\s*"shader"/);
assert.match(shaderBannerJavascript, /prepareIncomingScene/);
assert.match(shaderBannerJavascript, /promoteIncomingScene/);
assert.match(shaderBannerJavascript, /updateThoughtAssembly/);
assert.match(shaderBannerJavascript, /updateThoughtTail/);
assert.match(shaderBannerJavascript, /visibleCloudGeometry/);
assert.match(shaderBannerJavascript, /INCOMING_CODE_START/);
assert.match(shaderBannerJavascript, /layer\.path\.setAttribute\("transform"/);
assert.match(shaderBannerJavascript, /translate\(-1 -1\) scale\(3 3\)/);
assert.doesNotMatch(shaderBannerJavascript, /initialEntrance/);
assert.doesNotMatch(shaderBannerJavascript, /transitionEndsAt/);
assert.doesNotMatch(shaderBannerJavascript, /incomingLayer\.startedAt\s*=\s*timestamp/);
assert.doesNotMatch(shaderBannerJavascript, /thoughtPreview/);
assert.doesNotMatch(shaderBannerJavascript, /HTMLMediaElement|\.play\(|\.pause\(|webm|mp4|poster/);
assert.match(sharedRenderer, /getContext\("webgl2"/);
assert.match(sharedRenderer, /window\.devicePixelRatio \|\| 1/);
assert.doesNotMatch(sharedRenderer, /Math\.min\(window\.devicePixelRatio/);
assert.match(sharedRenderer, /gl\.vertexAttribPointer/);
assert.match(sharedRenderer, /gl\.drawArrays\(gl\.TRIANGLES/);
assert.match(sharedRenderer, /geometry\.vertexCount/);
assert.match(ideMain, /from "\.\.\/\.\.\/\.\.\/\.\.\/web\/shader-renderer\.js"/);

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
