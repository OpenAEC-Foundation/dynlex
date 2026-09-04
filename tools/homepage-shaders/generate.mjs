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

  const fragmentWgsl = `${fragment.wgsl.trimEnd()}\n`;
  writeOrCheck(scene.fragment, fragmentWgsl);
  generatedShaderPaths.add(scene.fragment);
  const shaders = {
    fragment: {
      path: scene.fragment.replace(/^web\//, ""),
      hash: sha256(fragmentWgsl),
      uniforms: fragment.uniforms
    }
  };

  if (scene.vertex) {
    const vertex = await compiler.compile(source, scene.source, "vertex");
    assertUniforms(vertex.uniforms, scene.source);
    const vertexWgsl = `${vertex.wgsl.trimEnd()}\n`;
    writeOrCheck(scene.vertex, vertexWgsl);
    generatedShaderPaths.add(scene.vertex);
    shaders.vertex = {
      path: scene.vertex.replace(/^web\//, ""),
      hash: sha256(vertexWgsl),
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
  schemaVersion: 11,
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
