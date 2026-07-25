import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHomepageShaderCompiler } from "./compiler.mjs";
import { shaderConfig } from "./config.mjs";

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
    const geometryData = requireBuffer(scene.geometry.path);
    const geometryMetadata = JSON.parse(requireFile(scene.geometry.metadata));
    if (
      geometryMetadata.schemaVersion !== 1
      || !Number.isInteger(geometryMetadata.pointCount)
      || geometryMetadata.pointCount <= 0
      || geometryData.byteLength !== geometryMetadata.pointCount * 3 * 4 * Float32Array.BYTES_PER_ELEMENT
    ) {
      throw new Error(`${scene.geometry.metadata} does not describe ${scene.geometry.path}`);
    }
    record.geometry = {
      path: scene.geometry.path.replace(/^web\//, ""),
      hash: sha256(geometryData),
      format: "float32x4",
      primitive: "triangles",
      pointCount: geometryMetadata.pointCount,
      vertexCount: geometryMetadata.pointCount * 3
    };
  }
  records.push(record);
}

const manifest = `${JSON.stringify({
  schemaVersion: 2,
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
