import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHomepageShaderCompiler } from "./compiler.mjs";
import { shaderConfig } from "./config.mjs";
import { generateTerrainLodGrid } from "./generate-terrain-grid.mjs";

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

function writeOrCheck(relativePath, content) {
  const outputPath = absolute(relativePath);
  if (checkOnly) {
    if (!fs.existsSync(outputPath) || fs.readFileSync(outputPath, "utf8") !== content) {
      throw new Error(`Generated shader output is stale: ${relativePath}`);
    }
    return;
  }
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const pendingPath = `${outputPath}.pending`;
  fs.writeFileSync(pendingPath, content);
  fs.renameSync(pendingPath, outputPath);
}

function writeBufferOrCheck(relativePath, content) {
  const outputPath = absolute(relativePath);
  if (checkOnly) {
    if (!fs.existsSync(outputPath) || !fs.readFileSync(outputPath).equals(content)) {
      throw new Error(`Generated geometry output is stale: ${relativePath}`);
    }
    return;
  }
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

function uint16IndexRecord(relativePath, data, count) {
  if (
    typeof relativePath !== "string"
    || relativePath.length === 0
    || !Number.isInteger(count)
    || count <= 0
    || data.byteLength !== count * Uint16Array.BYTES_PER_ELEMENT
  ) {
    throw new Error(`${relativePath} has an invalid index configuration`);
  }
  return {
    path: relativePath.replace(/^web\//, ""),
    hash: sha256(data),
    format: "uint16",
    count
  };
}

function pairedPointCloudRecord(geometry) {
  const geometryData = requireBuffer(geometry.path);
  const geometryMetadata = JSON.parse(requireFile(geometry.metadata));
  if (
    geometryMetadata.schemaVersion !== 4
    || !Number.isInteger(geometryMetadata.pointCount)
    || geometryMetadata.pointCount <= 0
    || geometryMetadata.motorcyclePointCount !== geometryMetadata.pointCount
    || !Number.isInteger(geometryMetadata.surfacePointCount)
    || !Number.isInteger(geometryMetadata.densityPointCount)
    || geometryMetadata.surfacePointCount
      + geometryMetadata.densityPointCount !== geometryMetadata.pointCount
    || geometryData.byteLength !== geometryMetadata.pointCount * 3 * 4 * Float32Array.BYTES_PER_ELEMENT
    || geometryMetadata.coordinateEncoding?.name !== geometry.attributeEncoding
    || geometryMetadata.coordinateEncoding?.quantizationLevels !== 4095
    || geometryMetadata.coordinateEncoding?.coordinateMinimum !== -2
    || geometryMetadata.coordinateEncoding?.coordinateMaximum !== 2
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
    const generated = generateTerrainLodGrid(
      geometry.terrainSampling,
      geometry.waterSampling
    );
    writeBufferOrCheck(geometry.path, generated.data);
    writeBufferOrCheck(geometry.indexPath, generated.indices);
    return geometryRecord(
      geometry,
      generated.data,
      generated.vertexCount,
      {
        indices: uint16IndexRecord(geometry.indexPath, generated.indices, generated.indexCount),
        surfaces: generated.surfaces
      }
    );
  }
  if (geometry.generator === "paired-point-cloud") {
    return pairedPointCloudRecord(geometry);
  }
  throw new Error(`Unsupported geometry generator: ${geometry.generator}`);
}

const compiler = await createHomepageShaderCompiler(projectDirectory);
const records = [];
let semanticLegend = null;
const generatedShaderPaths = new Set();

for (const scene of shaderConfig.scenes) {
  const source = requireFile(scene.source);
  const fragment = compiler.compile(source, scene.source, "fragment");
  assertUniforms(fragment.uniforms, scene.source);
  if (semanticLegend === null) {
    semanticLegend = fragment.semanticLegend;
  } else if (JSON.stringify(semanticLegend) !== JSON.stringify(fragment.semanticLegend)) {
    throw new Error("Compiler returned inconsistent semantic-token legends");
  }

  const fragmentGlsl = `${fragment.glsl.trimEnd()}\n`;
  writeOrCheck(scene.fragment, fragmentGlsl);
  generatedShaderPaths.add(scene.fragment);
  const shaders = {
    fragment: {
      path: scene.fragment.replace(/^web\//, ""),
      hash: sha256(fragmentGlsl)
    }
  };

  if (scene.vertex) {
    const vertex = compiler.compile(source, scene.source, "vertex");
    assertUniforms(vertex.uniforms, scene.source);
    if (JSON.stringify(fragment.uniforms) !== JSON.stringify(vertex.uniforms)) {
      throw new Error(`${scene.source} must expose identical uniforms in both shader stages`);
    }
    const vertexGlsl = `${vertex.glsl.trimEnd()}\n`;
    writeOrCheck(scene.vertex, vertexGlsl);
    generatedShaderPaths.add(scene.vertex);
    shaders.vertex = {
      path: scene.vertex.replace(/^web\//, ""),
      hash: sha256(vertexGlsl)
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
    uniforms: fragment.uniforms,
    semanticTokens: fragment.semanticTokens
  };

  if (scene.geometry) {
    record.geometry = configuredGeometryRecord(scene.geometry);
  }
  records.push(record);
}

const manifest = `${JSON.stringify({
  schemaVersion: 7,
  semanticLegend,
  scenes: records
}, null, 2)}\n`;
writeOrCheck(shaderConfig.manifest, manifest);

const generatedDirectory = absolute("web/shaders/generated");
if (fs.existsSync(generatedDirectory)) {
  for (const fileName of fs.readdirSync(generatedDirectory)) {
    const relativePath = `web/shaders/generated/${fileName}`;
    if (generatedShaderPaths.has(relativePath)) {
      continue;
    }
    if (checkOnly) {
      throw new Error(`Obsolete generated shader output remains: ${relativePath}`);
    }
    fs.rmSync(absolute(relativePath));
  }
}

console.log(`${checkOnly ? "Verified" : "Generated"} ${records.length} live homepage shaders.`);
