import path from "node:path";
import { pathToFileURL } from "node:url";

const buildDir = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve("build-web");
const modulePath = path.join(buildDir, "dynlex_web.js");

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

print "smoke"
`
]);

const status = moduleInstance.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);
const diagnosticsJson = moduleInstance.ccall("dynlex_web_get_diagnostics_json", "string", [], []);
const diagnosticsPayload = JSON.parse(diagnosticsJson);
if (!Array.isArray(diagnosticsPayload.diagnostics)) {
  throw new Error("Diagnostics payload is missing diagnostics array");
}

const wasmLength = moduleInstance.ccall("dynlex_web_get_output_wasm_len", "number", [], []);
if (status !== 0) {
  throw new Error(`Compile status ${status}. Diagnostics: ${diagnosticsJson}`);
}
if (diagnosticsPayload.diagnostics.length > 0) {
  throw new Error(`Expected no diagnostics, got: ${diagnosticsJson}`);
}
if (!(wasmLength > 8)) {
  throw new Error(`Expected emitted wasm length > 8, got ${wasmLength}`);
}

console.log(`DynLex web smoke passed (status=${status}, wasmLength=${wasmLength})`);
