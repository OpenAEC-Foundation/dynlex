import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHomepageShaderCompiler } from "./compiler.mjs";
import { shaderConfig } from "./config.mjs";
import { resolveTerrainGeometryDescriptor } from "../../web/terrain-geometry.js";

const toolDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(toolDirectory, "../..");
const checkOnly = process.argv.slice(2).includes("--check");

function absolute(relativePath) {
  return path.join(projectDirectory, relativePath);
}

function sha256(value) {
  return crypto.createHash("sha256").update(value).digest("hex");
}

function requireFile(relativePath) {
  const filePath = absolute(relativePath);
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    throw new Error(`Missing shader input: ${relativePath}`);
  }
  return fs.readFileSync(filePath, "utf8");
}

function requireBuffer(relativePath) {
  const filePath = absolute(relativePath);
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    throw new Error(`Missing shader input: ${relativePath}`);
  }
  return fs.readFileSync(filePath);
}

const translatorHash = sha256(Buffer.concat([
  Buffer.from(requireFile("tools/wgsl-translator/Cargo.toml")),
  Buffer.from(requireFile("tools/wgsl-translator/Cargo.lock")),
  Buffer.from(requireFile("tools/wgsl-translator/src/lib.rs")),
  requireBuffer("src/web/ide/public/compiler/dynlex_wgsl_translator.wasm")
]));
const compilerHash = sha256(Buffer.concat([
  Buffer.from(requireFile("tools/homepage-shaders/compiler.mjs")),
  requireBuffer("src/web/ide/public/compiler/dynlex_web.js"),
  requireBuffer("src/web/ide/public/compiler/dynlex_web.wasm")
]));

function collectDynLexInputs(entryPath, collected = new Map()) {
  if (collected.has(entryPath)) {
    return collected;
  }
  const source = requireFile(entryPath);
  collected.set(entryPath, source);
  for (const match of source.matchAll(/^\s*import\s+([^\s#]+)\s*$/gm)) {
    collectDynLexInputs(match[1], collected);
  }
  return collected;
}

function inputHash(entryPath) {
  const inputs = [...collectDynLexInputs(entryPath)]
    .sort(([left], [right]) => left.localeCompare(right));
  return sha256(inputs.map(([name, source]) => `${name}\0${source}`).join("\0"));
}

function assertUniforms(uniforms, sourceName) {
  const names = uniforms.map((uniform) => uniform.name);
  const required = ["time", "width", "height"];
  const supported = new Set([...required, "render_pass"]);
  if (
    !required.every((name) => names.includes(name))
    || names.some((name) => !supported.has(name))
    || new Set(names).size !== names.length
  ) {
    throw new Error(`${sourceName} exposes an unsupported shader-uniform interface`);
  }
}

function writeGenerated(relativePath, content) {
  const outputPath = absolute(relativePath);
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const pendingPath = `${outputPath}.pending`;
  fs.writeFileSync(pendingPath, content);
  fs.renameSync(pendingPath, outputPath);
}

function geometryRecord(geometry, data, vertexCount, additions = {}) {
  if (
    geometry.attributeEncoding.length === 0
    || typeof geometry.render?.backgroundPass !== "boolean"
    || !["opaque", "additive"].includes(geometry.render.blendMode)
    || typeof geometry.render.depthTest !== "boolean"
    || !Number.isInteger(vertexCount)
    || vertexCount <= 0
    || data.byteLength !== vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT
  ) {
    throw new Error(`${geometry.path} has an invalid geometry configuration`);
  }
  return {
    path: geometry.path.replace(/^web\//, ""),
    hash: sha256(data),
    format: "float32x4",
    attributeEncoding: geometry.attributeEncoding,
    primitive: "triangles",
    vertexCount,
    render: geometry.render,
    ...additions
  };
}

function pairedPointCloudRecord(geometry) {
  const geometryData = requireBuffer(geometry.path);
  const geometryMetadata = JSON.parse(requireFile(geometry.metadata));
  if (
    geometryMetadata.schemaVersion !== 5
    || !Number.isInteger(geometryMetadata.pointCount)
    || geometryMetadata.pointCount <= 0
    || geometryMetadata.motorcyclePointCount !== geometryMetadata.pointCount
    || !Number.isInteger(geometryMetadata.motorcycleWheelPointCount)
    || geometryMetadata.motorcycleWheelPointCount <= 0
    || geometryMetadata.motorcycleWheelPointCount >= geometryMetadata.pointCount
    || !Number.isInteger(geometryMetadata.surfacePointCount)
    || !Number.isInteger(geometryMetadata.densityPointCount)
    || geometryMetadata.surfacePointCount
      + geometryMetadata.densityPointCount !== geometryMetadata.pointCount
    || geometryData.byteLength !== geometryMetadata.pointCount * 3 * 4 * Float32Array.BYTES_PER_ELEMENT
    || geometryMetadata.attributeEncoding !== geometry.attributeEncoding
    || geometryMetadata.coordinateEncoding?.name !== "paired-unorm12"
    || geometryMetadata.coordinateEncoding?.quantizationLevels !== 4095
    || geometryMetadata.coordinateEncoding?.coordinateMinimum !== -2
    || geometryMetadata.coordinateEncoding?.coordinateMaximum !== 2
    || geometryMetadata.triangleCornerEncoding?.name !== "wheel-part-plus-corner"
    || geometryMetadata.triangleCornerEncoding?.wheelOffset !== 4
    || geometryMetadata.triangleCornerEncoding?.cornerCount !== 3
    || geometryMetadata.pointPairing?.name !== "recursive-spatial-bisection"
    || geometryMetadata.pointPairing?.leafPointCount !== 64
    || !Array.isArray(geometryMetadata.pointPairing?.axisOrder)
    || geometryMetadata.pointPairing.axisOrder.length !== 3
    || geometryMetadata.pointPairing.axisOrder.some(
      (axis, index) => axis !== ["x", "y", "z"][index]
    )
    || typeof geometryMetadata.source?.uid !== "string"
    || typeof geometryMetadata.source?.title !== "string"
    || typeof geometryMetadata.source?.author !== "string"
    || typeof geometryMetadata.source?.authorUrl !== "string"
    || typeof geometryMetadata.source?.url !== "string"
    || typeof geometryMetadata.source?.license !== "string"
    || typeof geometryMetadata.source?.licenseUrl !== "string"
    || !/^[a-f0-9]{64}$/.test(geometryMetadata.source?.archiveSha256)
    || !/^[a-f0-9]{64}$/.test(geometryMetadata.source?.meshSha256)
    || !Array.isArray(geometryMetadata.modifications)
    || geometryMetadata.modifications.length === 0
    || geometryMetadata.modifications.some((entry) => typeof entry !== "string")
  ) {
    throw new Error(`${geometry.metadata} does not describe ${geometry.path}`);
  }
  return geometryRecord(
    geometry,
    geometryData,
    geometryMetadata.pointCount * 3,
    {
      pointCount: geometryMetadata.pointCount,
      motorcycleWheelPointCount: geometryMetadata.motorcycleWheelPointCount,
      attribution: {
        title: geometryMetadata.source.title,
        author: geometryMetadata.source.author,
        authorUrl: geometryMetadata.source.authorUrl,
        sourceUrl: geometryMetadata.source.url,
        license: geometryMetadata.source.license,
        licenseUrl: geometryMetadata.source.licenseUrl,
        modifications: geometryMetadata.modifications
      }
    }
  );
}

function configuredGeometryRecord(geometry) {
  if (geometry.generator === "camera-lod-grid") {
    resolveTerrainGeometryDescriptor(
      {
        ...geometry,
        format: "float32x4",
        primitive: "triangles"
      },
      geometry.referenceWidthPixels
    );
    return {
      generator: geometry.generator,
      referenceWidthPixels: geometry.referenceWidthPixels,
      cameraDistance: geometry.cameraDistance,
      terrainSampling: geometry.terrainSampling,
      waterSampling: geometry.waterSampling,
      format: "float32x4",
      attributeEncoding: geometry.attributeEncoding,
      primitive: "triangles",
      render: geometry.render
    };
  }
  if (geometry.generator === "paired-point-cloud") {
    return pairedPointCloudRecord(geometry);
  }
  throw new Error(`Unsupported geometry generator: ${geometry.generator}`);
}

function verifyGeneratedOutputs() {
  const manifest = JSON.parse(requireFile(shaderConfig.manifest));
  if (
    manifest.schemaVersion !== 13
    || manifest.translatorHash !== translatorHash
    || manifest.compilerHash !== compilerHash
    || !Array.isArray(manifest.scenes)
    || manifest.scenes.length !== shaderConfig.scenes.length
  ) {
    throw new Error("Generated homepage shader manifest is stale");
  }
  const expectedPaths = new Set();
  for (let index = 0; index < shaderConfig.scenes.length; index += 1) {
    const scene = shaderConfig.scenes[index];
    const record = manifest.scenes[index];
    const source = requireFile(scene.source);
    if (
      record?.id !== scene.id
      || record.title !== scene.title
      || record.durationSeconds !== shaderConfig.durationSeconds
      || record.source !== source
      || record.sourceHash !== sha256(source)
      || record.inputHash !== inputHash(scene.source)
      || JSON.stringify(record.geometry) !== JSON.stringify(
        scene.geometry ? configuredGeometryRecord(scene.geometry) : undefined
      )
    ) {
      throw new Error(`Generated homepage shader record is stale: ${scene.id}`);
    }
    for (const stageName of ["fragment", ...(scene.vertex ? ["vertex"] : [])]) {
      const configuredStage = stageName === "fragment" ? scene.fragment : scene.vertex;
      const generatedStage = record.shaders?.[stageName];
      for (const backend of ["webgpu", "webgl"]) {
        const configuredPath = configuredStage[backend];
        const sourcePath = configuredPath.replace(/^web\//, "");
        const generatedSource = requireFile(configuredPath);
        if (
          generatedStage?.sources?.[backend]?.path !== sourcePath
          || generatedStage.sources[backend].hash !== sha256(generatedSource)
        ) {
          throw new Error(`Generated shader output is stale: ${configuredPath}`);
        }
        expectedPaths.add(configuredPath);
      }
    }
  }
  const actualPaths = fs.readdirSync(absolute("web/shaders/generated"))
    .map((fileName) => `web/shaders/generated/${fileName}`)
    .sort();
  if (JSON.stringify(actualPaths) !== JSON.stringify([...expectedPaths].sort())) {
    throw new Error("Generated shader directory contains stale or missing outputs");
  }
  console.log(`Verified ${manifest.scenes.length} live homepage shaders.`);
}

if (checkOnly) {
  verifyGeneratedOutputs();
  process.exit(0);
}

const compiler = await createHomepageShaderCompiler(projectDirectory);
const records = [];
let semanticLegend = null;
const generatedShaderPaths = new Set();

for (const scene of shaderConfig.scenes) {
  const source = requireFile(scene.source);
  const fragment = await compiler.compile(source, scene.source, "fragment");
  assertUniforms(fragment.uniforms, scene.source);
  if (semanticLegend === null) {
    semanticLegend = fragment.semanticLegend;
  } else if (JSON.stringify(semanticLegend) !== JSON.stringify(fragment.semanticLegend)) {
    throw new Error("Compiler returned inconsistent semantic-token legends");
  }

  const fragmentSources = {
    webgpu: `${fragment.wgsl.trimEnd()}\n`,
    webgl: `${fragment.glsl.trimEnd()}\n`
  };
  for (const backend of ["webgpu", "webgl"]) {
    writeGenerated(scene.fragment[backend], fragmentSources[backend]);
    generatedShaderPaths.add(scene.fragment[backend]);
  }
  const shaders = {
    fragment: {
      sources: Object.fromEntries(["webgpu", "webgl"].map((backend) => [
        backend,
        {
          path: scene.fragment[backend].replace(/^web\//, ""),
          hash: sha256(fragmentSources[backend])
        }
      ])),
      uniforms: fragment.uniforms
    }
  };

  if (scene.vertex) {
    const vertex = await compiler.compile(source, scene.source, "vertex");
    assertUniforms(vertex.uniforms, scene.source);
    const vertexSources = {
      webgpu: `${vertex.wgsl.trimEnd()}\n`,
      webgl: `${vertex.glsl.trimEnd()}\n`
    };
    for (const backend of ["webgpu", "webgl"]) {
      writeGenerated(scene.vertex[backend], vertexSources[backend]);
      generatedShaderPaths.add(scene.vertex[backend]);
    }
    shaders.vertex = {
      sources: Object.fromEntries(["webgpu", "webgl"].map((backend) => [
        backend,
        {
          path: scene.vertex[backend].replace(/^web\//, ""),
          hash: sha256(vertexSources[backend])
        }
      ])),
      uniforms: vertex.uniforms
    };
  }

  const record = {
    id: scene.id,
    title: scene.title,
    durationSeconds: shaderConfig.durationSeconds,
    source,
    sourceHash: sha256(source),
    inputHash: inputHash(scene.source),
    shaders,
    semanticTokens: fragment.semanticTokens
  };

  if (scene.geometry) {
    record.geometry = configuredGeometryRecord(scene.geometry);
  }
  records.push(record);
}

await compiler.close();

const manifest = `${JSON.stringify({
  schemaVersion: 13,
  translatorHash,
  compilerHash,
  semanticLegend,
  scenes: records
}, null, 2)}\n`;
writeGenerated(shaderConfig.manifest, manifest);

const generatedDirectory = absolute("web/shaders/generated");
if (fs.existsSync(generatedDirectory)) {
  for (const fileName of fs.readdirSync(generatedDirectory)) {
    const relativePath = `web/shaders/generated/${fileName}`;
    if (generatedShaderPaths.has(relativePath)) {
      continue;
    }
    fs.rmSync(absolute(relativePath));
  }
}

console.log(`Generated ${records.length} live homepage shaders.`);
