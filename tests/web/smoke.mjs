import assert from "node:assert/strict";
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

let nextLspRequestId = 1;
function exchangeLsp(message) {
  const json = moduleInstance.ccall(
    "dynlex_web_lsp_exchange_json",
    "string",
    ["string"],
    [JSON.stringify(message)]
  );
  const messages = JSON.parse(json);
  assert.ok(Array.isArray(messages), `LSP exchange must return an array, got: ${json}`);
  return messages;
}

function settleLspMessages(messages) {
  for (const message of messages) {
    if (typeof message?.method === "string" && Object.hasOwn(message, "id")) {
      const response = message.method === "workspace/semanticTokens/refresh"
        ? { jsonrpc: "2.0", id: message.id, result: null }
        : {
            jsonrpc: "2.0",
            id: message.id,
            error: { code: -32601, message: `Unsupported test client method: ${message.method}` }
          };
      settleLspMessages(exchangeLsp(response));
    }
  }
  return messages;
}

function requestLsp(method, params) {
  const id = nextLspRequestId++;
  const messages = settleLspMessages(exchangeLsp({ jsonrpc: "2.0", id, method, params }));
  const response = messages.find((message) => message?.id === id && !message.method);
  assert.ok(response, `LSP request ${method} returned no response: ${JSON.stringify(messages)}`);
  if (response.error) {
    throw new Error(`LSP request ${method} failed: ${JSON.stringify(response.error)}`);
  }
  return response.result;
}

function notifyLsp(method, params) {
  return settleLspMessages(exchangeLsp({ jsonrpc: "2.0", method, params }));
}

const initializeResult = requestLsp("initialize", {
  processId: null,
  rootUri: "file:///workspace",
  capabilities: {}
});
assert.equal(initializeResult.capabilities.textDocumentSync, 2);
assert.equal(initializeResult.capabilities.definitionProvider, true);
assert.equal(initializeResult.capabilities.hoverProvider, true);
assert.equal(initializeResult.capabilities.documentSymbolProvider, true);
assert.equal(initializeResult.capabilities.codeActionProvider, true);
assert.ok(initializeResult.capabilities.completionProvider);
assert.ok(initializeResult.capabilities.semanticTokensProvider);
notifyLsp("initialized", {});

const mainUri = "file:///workspace/main.dl";
const mainSource = `import lib/std.dl

function value squared:
    execute:
        return value * value

print 8 squared as a line
`;
moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [mainSource]);
const didOpenMessages = notifyLsp("textDocument/didOpen", {
  textDocument: {
    uri: mainUri,
    languageId: "dynlex",
    version: 1,
    text: mainSource
  }
});
const publishedDiagnostics = didOpenMessages.find(
  (message) => message?.method === "textDocument/publishDiagnostics" && message.params?.uri === mainUri
);
assert.ok(publishedDiagnostics, `didOpen published no diagnostics: ${JSON.stringify(didOpenMessages)}`);
assert.deepEqual(publishedDiagnostics.params.diagnostics, []);

const nestedImportUri = "file:///workspace/nested-import.dl";
const nestedImportSource = await readFile(
  path.resolve(import.meta.dirname, "../../tools/homepage-shaders/shaders/nano-choreography.dl"),
  "utf8"
);
const nestedImportMessages = notifyLsp("textDocument/didOpen", {
  textDocument: {
    uri: nestedImportUri,
    languageId: "dynlex",
    version: 1,
    text: nestedImportSource
  }
});
const nestedImportDiagnosticUris = nestedImportMessages
  .filter((message) => message?.method === "textDocument/publishDiagnostics")
  .map((message) => message.params?.uri);
assert.ok(
  nestedImportDiagnosticUris.some((uri) => uri?.endsWith("/lib/shader.dl")),
  `Nested web import did not publish the expected library diagnostic: ${JSON.stringify(nestedImportMessages)}`
);
for (const uri of nestedImportDiagnosticUris) {
  assert.match(uri, /^file:\/\/\/[^/]/, `LSP published a non-canonical document URI: ${uri}`);
}
notifyLsp("textDocument/didClose", { textDocument: { uri: nestedImportUri } });

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

const textDocument = { uri: mainUri };
const hoverPayload = requestLsp("textDocument/hover", {
  textDocument,
  position: { line: 6, character: 8 }
});
assert.ok(hoverPayload?.contents, `Expected hover payload, got: ${JSON.stringify(hoverPayload)}`);

const definitionPayload = requestLsp("textDocument/definition", {
  textDocument,
  position: { line: 6, character: 8 }
});
assert.equal(definitionPayload?.uri, mainUri);
const importedDefinitionPayload = requestLsp("textDocument/definition", {
  textDocument,
  position: { line: 6, character: 2 }
});
assert.notEqual(importedDefinitionPayload?.uri, mainUri);
assert.equal(
  typeof requestLsp("dynlex/readDocument", { uri: importedDefinitionPayload.uri }),
  "string"
);

const completionPayload = requestLsp("textDocument/completion", {
  textDocument,
  position: { line: 6, character: 3 }
});
assert.ok(Array.isArray(completionPayload?.items) && completionPayload.items.length > 0);

const documentSymbols = requestLsp("textDocument/documentSymbol", { textDocument });
assert.ok(Array.isArray(documentSymbols) && documentSymbols.some((symbol) => symbol.name.includes("squared")));

const semanticTokensPayload = requestLsp("textDocument/semanticTokens/full", { textDocument });
if (!Array.isArray(semanticTokensPayload.data) || semanticTokensPayload.data.length === 0) {
  throw new Error(`Expected semantic token data, got: ${JSON.stringify(semanticTokensPayload)}`);
}
if (semanticTokensPayload.data.length % 5 !== 0) {
  throw new Error(`Semantic token payload must be groups of 5, got ${semanticTokensPayload.data.length}`);
}
assert.ok(Array.isArray(initializeResult.capabilities.semanticTokensProvider.legend.tokenTypes));

const renderedSemanticTokens = requestLsp("dynlex/renderSemanticTokens", textDocument);
assert.equal(typeof renderedSemanticTokens, "string");
assert.ok(renderedSemanticTokens.length > mainSource.length);

const instantiations = requestLsp("dynlex/instantiationsInDocument", textDocument);
assert.ok(Array.isArray(instantiations));
assert.ok(
  instantiations.some((entry) => (
    entry.options.some((option) => option.label === "{a 32-bit integer:value} squared")
  )),
  `Expected typed squared-value instantiation label, got: ${JSON.stringify(instantiations)}`
);
notifyLsp("dynlex/activeCursorChanged", {
  clientId: "web-smoke",
  uri: mainUri,
  version: 1,
  position: { line: 6, character: 8 }
});

const documentText = requestLsp("dynlex/readDocument", textDocument);
assert.equal(documentText, mainSource);
const standardLibraryText = requestLsp("dynlex/readDocument", { uri: "file:///lib/std.dl" });
assert.match(standardLibraryText, /function|flex|import/);

const quickFixDiagnostic = {
  range: {
    start: { line: 6, character: 0 },
    end: { line: 6, character: 5 }
  },
  severity: 1,
  message: "synthetic quick fix",
  data: {
    quickFixes: [
      {
        title: "Replace print",
        replacement: "print",
        range: {
          start: { line: 6, character: 0 },
          end: { line: 6, character: 5 }
        },
        uri: mainUri
      }
    ]
  }
};
const codeActions = requestLsp("textDocument/codeAction", {
  textDocument,
  range: quickFixDiagnostic.range,
  context: { diagnostics: [quickFixDiagnostic] }
});
assert.equal(codeActions.length, 1);
assert.equal(codeActions[0].title, "Replace print");
assert.equal(codeActions[0].edit.changes[mainUri][0].newText, "print");

const changedSource = mainSource.replace("8 squared", "9 squared");
const didChangeMessages = notifyLsp("textDocument/didChange", {
  textDocument: { uri: mainUri, version: 2 },
  contentChanges: [
    {
      range: {
        start: { line: 6, character: 6 },
        end: { line: 6, character: 7 }
      },
      text: "9"
    }
  ]
});
assert.ok(
  didChangeMessages.some((message) => message?.method === "workspace/semanticTokens/refresh"),
  "Incremental document sync must request semantic-token refresh"
);
assert.equal(requestLsp("dynlex/readDocument", textDocument), changedSource);

moduleInstance.ccall("dynlex_web_set_main_source", null, ["string"], [
  `import lib/shader_art.dl

set pulse to the shader time
set pulse to the sine of pulse
set pulse to pulse saturated
set the fragment color with a red channel of pulse, a green channel of 0.2, a blue channel of 0.8 and an alpha channel of 1.0
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

set the output position with x the vertex x, y the vertex y, z the vertex z and w the vertex w
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
    set the shader interpolant named "surface" with an x coordinate of (the vertex x), a y coordinate of (the vertex y), a z coordinate of (the vertex z) and a w coordinate of 1.0
    set the output position with x (the vertex x), y (the vertex y), z (the vertex z) and w (the vertex w)

if this is a fragment shader:
    set shade to the shader interpolant x named "surface"
    set the fragment color with a red channel of shade, a green channel of shade, a blue channel of shade and an alpha channel of 1.0
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
if (runtimeOutput !== "64\n") {
  throw new Error(`Expected runtime output \"64\\n\", got ${JSON.stringify(runtimeOutput)}`);
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

write the string form of "saved" to the file at "session.txt" and print whether it succeeded as a line
append the string form of "-data" to the file at "session.txt" and print whether it succeeded as a line
copy the filesystem entry at "session.txt" to "copy.txt" and print whether it succeeded as a line
rename the filesystem entry at "copy.txt" to "moved.txt" and print whether it succeeded as a line
delete the filesystem entry at "session.txt" and print whether it succeeded
`);
if (writeOutput !== "true\ntrue\ntrue\ntrue\ntrue") {
  throw new Error(`Unexpected filesystem mutation output: ${JSON.stringify(writeOutput)}`);
}

const readOutput = await compileAndRun(`import lib/filesystem.dl

read the file at "moved.txt" and print whether it succeeded as a line
print the contents of it as a line
delete the filesystem entry at "moved.txt" and print whether it succeeded
`);
if (readOutput !== "true\nsaved-data\ntrue") {
  throw new Error(`Unexpected persistent filesystem output: ${JSON.stringify(readOutput)}`);
}

const metadataOutput = await compileAndRun(`import lib/filesystem.dl

create a directory at "metadata" and print whether it succeeded as a line
print whether "metadata" is a regular file as a line
print whether "metadata" is readable as a line
write the string form of "contents" to the file at "metadata/file.txt" and print whether it succeeded as a line
print whether "metadata/file.txt" is a regular file as a line
set entry to the file system entry at "metadata/file.txt"
print whether entry's modification time's seconds > 0 as a line
read the file at "missing.txt" and print whether it succeeded as a line
print whether the error message of it is not empty as a line
delete the filesystem entry at "metadata/file.txt"
delete the filesystem entry at "metadata"
`);
if (metadataOutput !== "true\nfalse\nfalse\ntrue\ntrue\ntrue\nfalse\ntrue\n") {
  throw new Error(`Unexpected filesystem metadata output: ${JSON.stringify(metadataOutput)}`);
}

const transactionOutput = await compileAndRun(`import lib/filesystem.dl

write the string form of "web" to the file at "transaction-source.txt"
set entry to the file system entry at "transaction-source.txt"
print whether entry is supported as a line
print whether entry lookup succeeded as a line
print whether entry was found as a line
print whether entry is a regular file as a line
print whether entry's mode is unsupported as a line
print whether entry supports modification times as a line
print whether entry's identity is unsupported as a line
create a staging file beside "transaction-source.txt" and set staging to it
print whether staging is unsupported as a line
print whether staging failed as a line
print whether staging's error message is not empty as a line
delete the filesystem entry at "transaction-source.txt"
`);
if (transactionOutput !== "true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n") {
  throw new Error(`Unexpected filesystem transaction-capability output: ${JSON.stringify(transactionOutput)}`);
}

const pathOutput = await compileAndRun(`import lib/path.dl

set style to the POSIX path style
set decoding to the path from "file:///tmp/%C3%A9%20path" interpreted as a file URI using style
print whether decoding succeeded as a line
print whether decoding is supported as a line
print whether (decoding's value = "/tmp/é path") as a line
set encoding to the file URI representing "/tmp/é path" using style
print whether ((encoding succeeded) and ((encoding is supported) and ((encoding's value) = "file:///tmp/%C3%A9%20path"))) as a line

set resolution to the path from (the string form of "file:///tmp/native") interpreted as a file URI
print whether resolution failed as a line
print whether resolution is unsupported as a line
print whether (resolution's value is empty) as a line
print whether resolution's error message is not empty as a line
set reference to the file URI for (the string form of "/tmp/native") interpreted as a native path
print whether reference failed as a line
print whether reference is unsupported as a line
print whether reference's error message is not empty as a line
`);
if (pathOutput !== "true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n") {
  throw new Error(`Unexpected browser path output: ${JSON.stringify(pathOutput)}`);
}

const hostOutput = await compileAndRun(`import lib/host.dl

set executable to the running executable path
set directory to the running executable directory
set platform to whether the host platform is Windows
read a chunk from the standard input and set input to it
print whether executable failed as a line
print whether executable is unsupported as a line
print whether executable's error message is not empty as a line
print whether directory failed as a line
print whether directory is unsupported as a line
print whether directory's error message is not empty as a line
print whether platform failed as a line
print whether platform is unsupported as a line
print whether platform's error message is not empty as a line
print whether input failed as a line
print whether input is unsupported as a line
print whether the length of input's contents is 0 as a line
print whether input has not reached the end of file as a line
print whether input's error message is not empty as a line
`);
if (hostOutput !== "true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n") {
  throw new Error(`Unexpected browser host output: ${JSON.stringify(hostOutput)}`);
}

const variadicOutput = await compileAndRun(
  `function print a variadic sample:
    replacement:
        @intrinsic("discard", @intrinsic("variadic call", "libc", "printf", @intrinsic("type", "int", 32), 1, "%d %.1f %d %s\\n", @intrinsic("cast", 7, @intrinsic("type", "int", 8)), @intrinsic("cast", 1.5, @intrinsic("type", "float", 32)), @intrinsic("cast", 1, @intrinsic("type", "bool")), "ok"))

print a variadic sample`
);
if (variadicOutput !== "7 1.5 1 ok\n") {
  throw new Error(`Unexpected variadic runtime output: ${JSON.stringify(variadicOutput)}`);
}

const targetLayoutOutput = await compileAndRun(`import lib/std.dl

class:
    patterns:
        [a|] layout sample
    members:
        marker as a byte
        text as a string
        count as a 32 bit integer

class:
    patterns:
        [an|] aligned layout sample
    alignment:
        16
    members:
        marker as a byte
        padding:
            8
        count as a 32 bit integer
        tail as a byte

function an aligned layout sample with marker {byte:marker}, count {a 32 bit integer:count} and tail {byte:tail}:
    replacement:
        @intrinsic("construct", an aligned layout sample, marker, count, tail)

print the size of a string as a line
print the size of a layout sample as a line
print the size of a C long integer as a line
print the size of an aligned layout sample as a line
set sample to an aligned layout sample with marker 1 as a byte, count 42 and tail 2 as a byte
print sample's count as a line
print sample's tail as a line
`);
if (targetLayoutOutput !== "12\n20\n4\n16\n42\n2\n") {
  throw new Error(`Unexpected wasm target layout output: ${JSON.stringify(targetLayoutOutput)}`);
}

moduleInstance.ccall("dynlex_web_init", null, [], []);
nextLspRequestId = 1;
const shaderInitializeResult = requestLsp("initialize", {
  processId: null,
  rootUri: "file:///workspace",
  capabilities: {},
  initializationOptions: {
    dynlex: {
      analysisProfiles: [
        { target: "spirv", shaderStage: "fragment" },
        { target: "spirv", shaderStage: "vertex" }
      ]
    }
  }
});
assert.equal(shaderInitializeResult.capabilities.hoverProvider, true);
notifyLsp("initialized", {});

const shaderHoverUri = "file:///workspace/nano-choreography.dl";
const shaderHoverSource = nestedImportSource;
const shaderHoverMessages = notifyLsp("textDocument/didOpen", {
  textDocument: {
    uri: shaderHoverUri,
    languageId: "dynlex",
    version: 1,
    text: shaderHoverSource
  }
});
const shaderDiagnostics = shaderHoverMessages
  .filter((message) => message?.method === "textDocument/publishDiagnostics")
  .flatMap((message) => message.params.diagnostics);
assert.equal(
  shaderDiagnostics.some((diagnostic) => diagnostic.severity === 1),
  false,
  `Dual-stage shader LSP analysis reported errors: ${JSON.stringify(shaderDiagnostics)}`
);
const shaderHover = requestLsp("textDocument/hover", {
  textDocument: { uri: shaderHoverUri },
  position: { line: 84, character: 14 }
});
assert.ok(
  shaderHover?.contents,
  `Expected shader hover payload, got ${JSON.stringify(shaderHover)} after ${JSON.stringify(shaderHoverMessages)}`
);
const shaderSemanticTokens = requestLsp("textDocument/semanticTokens/full", {
  textDocument: { uri: shaderHoverUri }
});
assert.ok(
  Array.isArray(shaderSemanticTokens?.data) && shaderSemanticTokens.data.length > 0,
  `Expected dual-stage shader semantic tokens, got ${JSON.stringify(shaderSemanticTokens)}`
);
assert.ok(
  new Set(shaderSemanticTokens.data.filter((_, index) => index % 5 === 3)).size >= 3,
  `Expected several shader semantic token types, got ${JSON.stringify(shaderSemanticTokens)}`
);
notifyLsp("textDocument/didClose", { textDocument: { uri: shaderHoverUri } });

console.log(`DynLex web smoke passed (status=${status}, wasmLength=${wasmLength})`);
