import path from "node:path";
import { readFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  inspectRuntimeWasmLayout,
  isSupportedRuntimeImport
} from "../../src/web/ide/public/compiler/runtimeImports.js";

const buildDir = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve("build-web");
const modulePath = path.join(buildDir, "dynlex_web.js");
const moduleGlue = await readFile(modulePath, "utf8");
if (!moduleGlue.includes("__cxa_begin_catch")) {
  throw new Error("Browser compiler was built without C++ exception catching");
}

const imported = await import(pathToFileURL(modulePath).href);
const createModule = imported.default ?? imported;
if (typeof createModule !== "function") {
  throw new Error(`Expected module factory export from ${modulePath}`);
}

const moduleInstance = await createModule({
  locateFile(fileName) {
    return path.join(buildDir, fileName);
  }
});

moduleInstance.ccall("dynlex_web_init", null, [], []);
moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [
  `import lib/std.dl

set answer to 42
print answer
`
]);

const status = moduleInstance.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);
const diagnosticsJson = moduleInstance.ccall("dynlex_web_get_diagnostics_json", "string", [], []);
const diagnosticsPayload = JSON.parse(diagnosticsJson);
if (!Array.isArray(diagnosticsPayload.diagnostics)) {
  throw new Error("Diagnostics payload is missing diagnostics array");
}

const wasmLength = moduleInstance.ccall("dynlex_web_get_output_wasm_len", "number", [], []);
const wasmBase64 = moduleInstance.ccall("dynlex_web_get_output_wasm_base64", "string", [], []);
if (status !== 0) {
  throw new Error(`Compile status ${status}. Diagnostics: ${diagnosticsJson}`);
}
if (diagnosticsPayload.diagnostics.length > 0) {
  throw new Error(`Expected no diagnostics, got: ${diagnosticsJson}`);
}
if (!(wasmLength > 8)) {
  throw new Error(`Expected emitted wasm length > 8, got ${wasmLength}`);
}
if (typeof wasmBase64 !== "string" || wasmBase64.length === 0) {
  throw new Error("Expected emitted wasm base64 payload");
}

const hoverJson = moduleInstance.ccall("dynlex_web_get_lsp_hover_json", "string", ["number", "number"], [3, 8]);
const hoverPayload = JSON.parse(hoverJson);
if (hoverPayload && hoverPayload.error) {
  throw new Error(`Hover request failed: ${hoverPayload.error}`);
}
if (!hoverPayload || !hoverPayload.contents) {
  throw new Error(`Expected hover payload, got: ${hoverJson}`);
}

const definitionJson = moduleInstance.ccall("dynlex_web_get_lsp_definition_json", "string", ["number", "number"], [3, 8]);
const definitionPayload = JSON.parse(definitionJson);
if (definitionPayload && definitionPayload.error) {
  throw new Error(`Definition request failed: ${definitionPayload.error}`);
}
if (!definitionPayload || typeof definitionPayload.uri !== "string") {
  throw new Error(`Expected go-to-definition payload, got: ${definitionJson}`);
}

const semanticTokensJson = moduleInstance.ccall("dynlex_web_get_lsp_semantic_tokens_json", "string", [], []);
const semanticTokensPayload = JSON.parse(semanticTokensJson);
if (semanticTokensPayload && semanticTokensPayload.error) {
  throw new Error(`Semantic token request failed: ${semanticTokensPayload.error}`);
}
if (!Array.isArray(semanticTokensPayload.data) || semanticTokensPayload.data.length === 0) {
  throw new Error(`Expected semantic token data, got: ${semanticTokensJson}`);
}
if (semanticTokensPayload.data.length % 5 !== 0) {
  throw new Error(`Semantic token payload must be groups of 5, got ${semanticTokensPayload.data.length}`);
}
if (!semanticTokensPayload.legend || !Array.isArray(semanticTokensPayload.legend.tokenTypes)) {
  throw new Error(`Missing semantic token legend in payload: ${semanticTokensJson}`);
}

moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [
  `import lib/shader_art.dl

set pulse to the shader time
set pulse to the sine of pulse
set pulse to saturate pulse
set the fragment color to pulse 0.2 0.8 1.0
`
]);
const shaderStatus = moduleInstance.ccall(
  "dynlex_web_compile_and_emit_shader_glsl",
  "number",
  ["string"],
  ["fragment"]
);
const shaderDiagnosticsJson = moduleInstance.ccall("dynlex_web_get_diagnostics_json", "string", [], []);
const shaderGlsl = moduleInstance.ccall("dynlex_web_get_output_shader_glsl", "string", [], []);
const shaderUniformsJson = moduleInstance.ccall("dynlex_web_get_shader_uniforms_json", "string", [], []);
if (shaderStatus !== 0) {
  throw new Error(`Shader compile status ${shaderStatus}. Diagnostics: ${shaderDiagnosticsJson}`);
}
if (!shaderGlsl.startsWith("#version 300 es") || !shaderGlsl.includes("void main")) {
  throw new Error(`Expected WebGL2 fragment source, got: ${shaderGlsl}`);
}
const shaderUniforms = JSON.parse(shaderUniformsJson);
if (!Array.isArray(shaderUniforms.uniforms) || shaderUniforms.uniforms.length !== 1) {
  throw new Error(`Expected one reflected shader uniform, got: ${shaderUniformsJson}`);
}
if (shaderUniforms.uniforms[0].name !== "time" || shaderUniforms.uniforms[0].binding !== 0) {
  throw new Error(`Unexpected reflected shader uniform: ${shaderUniformsJson}`);
}

moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [
  `import lib/shader.dl

set the output position to the vertex x the vertex y the vertex z the vertex w
`
]);
const vertexShaderStatus = moduleInstance.ccall(
  "dynlex_web_compile_and_emit_shader_glsl",
  "number",
  ["string"],
  ["vertex"]
);
const vertexShaderDiagnosticsJson = moduleInstance.ccall(
  "dynlex_web_get_diagnostics_json",
  "string",
  [],
  []
);
const vertexShaderGlsl = moduleInstance.ccall("dynlex_web_get_output_shader_glsl", "string", [], []);
if (vertexShaderStatus !== 0) {
  throw new Error(
    `Vertex shader compile status ${vertexShaderStatus}. Diagnostics: ${vertexShaderDiagnosticsJson}`
  );
}
if (!vertexShaderGlsl.startsWith("#version 300 es") || !vertexShaderGlsl.includes("gl_Position")) {
  throw new Error(`Expected WebGL2 vertex source, got: ${vertexShaderGlsl}`);
}

moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [
  `import lib/shader.dl

if this is a vertex shader:
    set the shader interpolant named "surface" to (the vertex x) (the vertex y) (the vertex z) 1.0
    set the output position to (the vertex x) (the vertex y) (the vertex z) (the vertex w)

if this is a fragment shader:
    set shade to the shader interpolant x named "surface"
    set the fragment color to shade shade shade 1.0
`
]);
const interpolantGlsl = {};
for (const stage of ["fragment", "vertex"]) {
  const status = moduleInstance.ccall(
    "dynlex_web_compile_and_emit_shader_glsl",
    "number",
    ["string"],
    [stage]
  );
  const diagnostics = moduleInstance.ccall("dynlex_web_get_diagnostics_json", "string", [], []);
  if (status !== 0) {
    throw new Error(`Shader interpolant ${stage} compile failed: ${diagnostics}`);
  }
  interpolantGlsl[stage] = moduleInstance.ccall(
    "dynlex_web_get_output_shader_glsl",
    "string",
    [],
    []
  );
}
const surfaceInterpolantName = "dynlex_interpolant_73757266616365";
if (
  !interpolantGlsl.vertex.includes(surfaceInterpolantName)
  || !interpolantGlsl.fragment.includes(surfaceInterpolantName)
) {
  throw new Error(`Named shader interpolant did not survive WebGL translation: ${
    JSON.stringify(interpolantGlsl)
  }`);
}

moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], ["this shader does not compile"]);
const invalidShaderStatus = moduleInstance.ccall(
  "dynlex_web_compile_and_emit_shader_glsl",
  "number",
  ["string"],
  ["fragment"]
);
const invalidShaderGlsl = moduleInstance.ccall("dynlex_web_get_output_shader_glsl", "string", [], []);
if (invalidShaderStatus === 0) {
  throw new Error("Invalid shader source compiled successfully");
}
if (invalidShaderGlsl !== "") {
  throw new Error("A failed shader compilation retained a stale GLSL artifact");
}

const wasmBytes = Uint8Array.from(Buffer.from(wasmBase64, "base64"));
const wasmModule = await WebAssembly.compile(wasmBytes);
const importSpecs = WebAssembly.Module.imports(wasmModule);
const stdoutChunks = [];
const runtimeFilesystem = createRuntimeFilesystem();
const runtimeImports = buildRuntimeImports(
  importSpecs,
  stdoutChunks,
  runtimeFilesystem,
  inspectRuntimeWasmLayout(wasmBytes)
);
const instance = await WebAssembly.instantiate(wasmModule, runtimeImports);
const entryPoint = instance.exports.main ?? instance.exports._start;
if (typeof entryPoint !== "function") {
  throw new Error("Expected program entry point export named 'main' or '_start'");
}
entryPoint();
const runtimeOutput = stdoutChunks.join("");
if (runtimeOutput !== "42") {
  throw new Error(`Expected runtime output \"42\", got ${JSON.stringify(runtimeOutput)}`);
}

async function compileAndRun(source) {
  moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [source]);
  const compileStatus = moduleInstance.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);
  const compileDiagnostics = moduleInstance.ccall("dynlex_web_get_diagnostics_json", "string", [], []);
  if (compileStatus !== 0) {
    throw new Error(`Compile status ${compileStatus}. Diagnostics: ${compileDiagnostics}`);
  }
  const payload = JSON.parse(compileDiagnostics);
  if (!Array.isArray(payload.diagnostics) || payload.diagnostics.length > 0) {
    throw new Error(`Expected no diagnostics, got: ${compileDiagnostics}`);
  }

  const compiledBase64 = moduleInstance.ccall("dynlex_web_get_output_wasm_base64", "string", [], []);
  const compiledBytes = Uint8Array.from(Buffer.from(compiledBase64, "base64"));
  const compiledModule = await WebAssembly.compile(compiledBytes);
  const compiledImports = WebAssembly.Module.imports(compiledModule);
  const unsupported = compiledImports.filter((importSpec) => !isSupportedRuntimeImport(importSpec));
  if (unsupported.length > 0) {
    throw new Error(
      `Unsupported runtime imports: ${unsupported.map(({ module, name }) => `${module}.${name}`).join(", ")}`
    );
  }

  const output = [];
  const imports = buildRuntimeImports(
    compiledImports,
    output,
    runtimeFilesystem,
    inspectRuntimeWasmLayout(compiledBytes)
  );
  const compiledInstance = await WebAssembly.instantiate(compiledModule, imports);
  const compiledEntryPoint = compiledInstance.exports.main ?? compiledInstance.exports._start;
  if (typeof compiledEntryPoint !== "function") {
    throw new Error("Expected filesystem program entry point export named 'main' or '_start'");
  }
  compiledEntryPoint();
  return output.join("");
}

const writeOutput = await compileAndRun(`import lib/filesystem.dl

write the string form of "saved" to "session.txt" and print if it succeeded
print "" as line
append the string form of "-data" to "session.txt" and print if it succeeded
print "" as line
copy "session.txt" to "copy.txt" and print if it succeeded
print "" as line
rename "copy.txt" to "moved.txt" and print if it succeeded
print "" as line
delete "session.txt" and print if it succeeded
`);
if (writeOutput !== "1\n1\n1\n1\n1") {
  throw new Error(`Unexpected filesystem mutation output: ${JSON.stringify(writeOutput)}`);
}

const readOutput = await compileAndRun(`import lib/filesystem.dl

read "moved.txt" and print if it succeeded
print "" as line
print (the contents of it) as line
delete "moved.txt" and print if it succeeded
`);
if (readOutput !== "1\nsaved-data\n1") {
  throw new Error(`Unexpected persistent filesystem output: ${JSON.stringify(readOutput)}`);
}

const metadataOutput = await compileAndRun(`import lib/filesystem.dl

create directory at "metadata" and print if it succeeded
print "" as line
print "metadata" is a regular file as line
print "metadata" is readable as line
write the string form of "contents" to "metadata/file.txt" and print if it succeeded
print "" as line
print "metadata/file.txt" is a regular file as line
set metadata_entry to the file system entry at "metadata/file.txt"
print (metadata_entry's modification time)'s seconds > 0 as line
read "missing.txt" and print if it succeeded
print "" as line
print (the length of the error message of it) > 0 as line
delete "metadata/file.txt"
delete "metadata"
`);
if (metadataOutput !== "1\n0\n0\n1\n1\n1\n0\n1\n") {
  throw new Error(`Unexpected filesystem metadata output: ${JSON.stringify(metadataOutput)}`);
}

const transactionOutput = await compileAndRun(`import lib/filesystem.dl

write the string form of "web" to "transaction-source.txt"
set entry to the file system entry at "transaction-source.txt"
print entry's supported as line
print entry's succeeded as line
print entry's found as line
print entry's regular file as line
print (not entry's mode supported) as line
print entry's modification time supported as line
print (not entry's identity's supported) as line
create a staging file beside "transaction-source.txt"
set staging to it
print (not staging's supported) as line
print (not staging's succeeded) as line
print ((the length of staging's error message) > 0) as line
delete "transaction-source.txt"
`);
if (transactionOutput !== "1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n") {
  throw new Error(`Unexpected filesystem transaction-capability output: ${JSON.stringify(transactionOutput)}`);
}

const pathOutput = await compileAndRun(`import lib/path.dl

set posix to the POSIX path style
set explicit_path to a path from file URI "file:///tmp/%C3%A9%20path" using posix
print explicit_path's succeeded as line
print explicit_path's supported as line
print (explicit_path's value = "/tmp/é path") as line
set explicit_uri to a file URI for "/tmp/é path" using posix
print (explicit_uri's succeeded and (explicit_uri's supported and (explicit_uri's value = "file:///tmp/%C3%A9%20path"))) as line

set native_resolution to resolve file URI the string form of "file:///tmp/native"
print (not native_resolution's succeeded) as line
print (not native_resolution's supported) as line
print (native_resolution's value = "") as line
print (not (native_resolution's error message = "")) as line
set native_uri to a file URI from native path the string form of "/tmp/native"
print (not native_uri's succeeded) as line
print (not native_uri's supported) as line
print (not (native_uri's error message = "")) as line
`);
if (pathOutput !== "1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n") {
  throw new Error(`Unexpected browser path output: ${JSON.stringify(pathOutput)}`);
}

const hostOutput = await compileAndRun(`import lib/host.dl

set executable to the running executable path
set directory to the running executable directory
set platform to whether the host platform is Windows
set input_chunk to read a chunk from standard input
print (not executable's succeeded) as line
print (not executable's supported) as line
print (not (executable's error message = "")) as line
print (not directory's succeeded) as line
print (not directory's supported) as line
print (not (directory's error message = "")) as line
print (not platform's succeeded) as line
print (not platform's supported) as line
print (not (platform's error message = "")) as line
print (not input_chunk's succeeded) as line
print (not input_chunk's supported) as line
print (input_chunk's contents = "") as line
print (not input_chunk's end of file) as line
print (not (input_chunk's error message = "")) as line
`);
if (hostOutput !== "1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n1\n") {
  throw new Error(`Unexpected browser host output: ${JSON.stringify(hostOutput)}`);
}

const variadicOutput = await compileAndRun(
  `@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int", 32), 1, "%d %.1f %d %s\\n", @intrinsic("cast", 7, @intrinsic("type", "int", 8)), @intrinsic("cast", 1.5, @intrinsic("type", "float", 32)), @intrinsic("cast", 1, @intrinsic("type", "bool")), "ok"))`
);
if (variadicOutput !== "7 1.5 1 ok\n") {
  throw new Error(`Unexpected variadic runtime output: ${JSON.stringify(variadicOutput)}`);
}

const targetLayoutOutput = await compileAndRun(`import lib/std.dl

class:
    patterns:
        [a|] layout sample
    members:
        marker as byte
        text as string
        count as i32

class:
    patterns:
        [an|a|] aligned layout sample
    alignment:
        16
    members:
        marker as byte
        padding:
            8
        count as i32
        tail as byte

print the size of a string as line
print the size of a layout sample as line
print the size of a c long integer as line
print the size of an aligned layout sample as line
set aligned to @intrinsic("construct", an aligned layout sample, 1 as a byte, 42, 2 as a byte)
print the count of aligned as line
print the tail of aligned as line
`);
if (targetLayoutOutput !== "12\n20\n4\n16\n42\n2\n") {
  throw new Error(`Unexpected wasm target layout output: ${JSON.stringify(targetLayoutOutput)}`);
}

console.log(`DynLex web smoke passed (status=${status}, wasmLength=${wasmLength})`);
