import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  inspectRuntimeWasmLayout,
  isSupportedRuntimeImport,
  toNonNegativeInteger
} from "./runtimeImports.js";

const compilerBasePath = "/compiler/";

const state = {
  compilerModule: null,
  initialized: false,
  lastSuccessfulWasm: null,
  runtimeFilesystem: createRuntimeFilesystem(),
  artifactVersion: 0,
  syncedSource: "",
  syncedVersion: -1
};

function postResponse(id, ok, payload, error) {
  self.postMessage({ id, ok, payload, error });
}

function parseJsonOr(text, fallback) {
  if (!text) {
    return fallback;
  }
  try {
    return JSON.parse(text);
  } catch {
    return fallback;
  }
}

function decodeBase64ToBytes(base64Text) {
  if (!base64Text) {
    return new Uint8Array(0);
  }
  const binaryText = atob(base64Text);
  const bytes = new Uint8Array(binaryText.length);
  for (let i = 0; i < binaryText.length; i += 1) {
    bytes[i] = binaryText.charCodeAt(i);
  }
  return bytes;
}

function extractCompiledWasmBytes(module, wasmPointer, wasmLength) {
  const pointer = toNonNegativeInteger(wasmPointer);
  const length = toNonNegativeInteger(wasmLength);
  if (length <= 0) {
    return new Uint8Array(0);
  }

  const base64Payload = module.ccall("dynlex_web_get_output_wasm_base64", "string", [], []);
  if (typeof base64Payload === "string" && base64Payload.length > 0) {
    const bytes = decodeBase64ToBytes(base64Payload);
    if (bytes.length !== length) {
      throw new Error(
        `Compiler artifact length mismatch (base64=${bytes.length}, reported=${length}, ptr=${pointer}).`
      );
    }
    return bytes;
  }

  if (module.HEAPU8 && typeof module.HEAPU8.slice === "function") {
    return module.HEAPU8.slice(pointer, pointer + length);
  }

  throw new Error(`Compiler artifact bytes unavailable (ptr=${pointer}, len=${length}).`);
}

async function ensureCompilerInitialized() {
  if (state.initialized && state.compilerModule) {
    return;
  }

  const imported = await import(/* @vite-ignore */ `${compilerBasePath}dynlex_web.js`);
  const createModule = imported.default ?? imported;
  if (typeof createModule !== "function") {
    throw new Error("dynlex_web.js did not export a module factory.");
  }

  state.compilerModule = await createModule({
    locateFile(path) {
      return `${compilerBasePath}${path}`;
    }
  });
  state.compilerModule.ccall("dynlex_web_init", null, [], []);
  state.initialized = true;
}

function syncCompilerSource(source, version) {
  const module = state.compilerModule;
  const nextSource = typeof source === "string" ? source : "";
  const nextVersion = Number.isInteger(version) ? version : -1;
  const sourceChanged = nextSource !== state.syncedSource;
  const versionChanged = nextVersion !== state.syncedVersion;
  if (!sourceChanged && !versionChanged) {
    return;
  }
  module.ccall("dynlex_web_set_main_source", null, ["string"], [nextSource]);
  state.syncedSource = nextSource;
  state.syncedVersion = nextVersion;
}

function compileSource(source, version) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const status = module.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);

  const diagnosticsPayload = parseJsonOr(
    module.ccall("dynlex_web_get_diagnostics_json", "string", [], []),
    { diagnostics: [] }
  );
  const compilerLogPayload = parseJsonOr(
    module.ccall("dynlex_web_get_compiler_log_json", "string", [], []),
    { messages: [] }
  );

  if (status === 0) {
    const wasmLength = module.ccall("dynlex_web_get_output_wasm_len", "number", [], []);
    const wasmPointer = module.ccall("dynlex_web_get_output_wasm_ptr", "number", [], []);
    if (wasmLength > 0) {
      state.lastSuccessfulWasm = extractCompiledWasmBytes(module, wasmPointer, wasmLength);
      state.artifactVersion += 1;
    }
  }

  return {
    status,
    diagnostics: diagnosticsPayload.diagnostics ?? [],
    compilerLog: compilerLogPayload.messages ?? [],
    hasArtifact: !!state.lastSuccessfulWasm,
    artifactVersion: state.artifactVersion
  };
}

function extractLspError(payload) {
  if (payload && typeof payload === "object" && typeof payload.error === "string" && payload.error.length > 0) {
    return payload.error;
  }
  return "";
}

function getLspHover(source, version, line, column) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const hoverPayload = parseJsonOr(
    module.ccall("dynlex_web_get_lsp_hover_json", "string", ["number", "number"], [line, column]),
    null
  );
  const error = extractLspError(hoverPayload);
  if (error) {
    throw new Error(error);
  }
  return hoverPayload;
}

function getLspDefinition(source, version, line, column) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const definitionPayload = parseJsonOr(
    module.ccall("dynlex_web_get_lsp_definition_json", "string", ["number", "number"], [line, column]),
    null
  );
  const error = extractLspError(definitionPayload);
  if (error) {
    throw new Error(error);
  }
  return definitionPayload;
}

function getLspSemanticTokens(source, version) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const semanticPayload = parseJsonOr(
    module.ccall("dynlex_web_get_lsp_semantic_tokens_json", "string", [], []),
    { data: [], legend: { tokenTypes: [], tokenModifiers: [] } }
  );
  const error = extractLspError(semanticPayload);
  if (error) {
    throw new Error(error);
  }
  if (!Array.isArray(semanticPayload.data)) {
    semanticPayload.data = [];
  }
  if (!semanticPayload.legend || typeof semanticPayload.legend !== "object") {
    semanticPayload.legend = { tokenTypes: [], tokenModifiers: [] };
  }
  if (!Array.isArray(semanticPayload.legend.tokenTypes)) {
    semanticPayload.legend.tokenTypes = [];
  }
  if (!Array.isArray(semanticPayload.legend.tokenModifiers)) {
    semanticPayload.legend.tokenModifiers = [];
  }
  return semanticPayload;
}

async function runLastSuccessfulProgram() {
  if (!state.lastSuccessfulWasm) {
    return {
      stdout: "",
      error: "No successful build artifact is available. Compile successfully first.",
      artifactVersion: state.artifactVersion
    };
  }

  const module = await WebAssembly.compile(state.lastSuccessfulWasm);
  const imports = WebAssembly.Module.imports(module);
  const unsupportedImports = imports.filter((importSpec) => !isSupportedRuntimeImport(importSpec));
  if (unsupportedImports.length > 0) {
    const unsupportedNames = unsupportedImports
      .map((importSpec) => `${importSpec.module}.${importSpec.name}`)
      .join(", ");
    return {
      stdout: "",
      error: `Unsupported runtime imports for browser console runner: ${unsupportedNames}`,
      artifactVersion: state.artifactVersion
    };
  }

  const stdoutChunks = [];
  const runtimeImports = buildRuntimeImports(
    imports,
    stdoutChunks,
    state.runtimeFilesystem,
    inspectRuntimeWasmLayout(state.lastSuccessfulWasm)
  );

  let instance;
  try {
    instance = await WebAssembly.instantiate(module, runtimeImports);
  } catch (error) {
    return {
      stdout: "",
      error: `Failed to instantiate program WASM: ${error instanceof Error ? error.message : String(error)}`,
      artifactVersion: state.artifactVersion
    };
  }

  const entryPoint = instance.exports.main ?? instance.exports._start;
  if (typeof entryPoint !== "function") {
    return {
      stdout: stdoutChunks.join(""),
      error: "Program has no exported entry point named 'main' or '_start'.",
      artifactVersion: state.artifactVersion
    };
  }

  try {
    entryPoint();
  } catch (error) {
    return {
      stdout: stdoutChunks.join(""),
      error: `Program execution failed: ${error instanceof Error ? error.message : String(error)}`,
      artifactVersion: state.artifactVersion
    };
  }

  return {
    stdout: stdoutChunks.join(""),
    error: null,
    artifactVersion: state.artifactVersion
  };
}

// Worker protocol.
// - init -> initResult
// - compile { source, version } -> compileResult
// - run -> runResult
// - lsp.hover { source, version, line, column } -> hover|null
// - lsp.definition { source, version, line, column } -> location|null
// - lsp.semanticTokens { source, version } -> { data, legend }
self.onmessage = async (event) => {
  const { id, type, payload } = event.data ?? {};
  if (typeof id !== "number" || typeof type !== "string") {
    return;
  }

  try {
    if (type === "init") {
      await ensureCompilerInitialized();
      postResponse(id, true, { ready: true });
      return;
    }

    if (type === "compile") {
      await ensureCompilerInitialized();
      const source = typeof payload?.source === "string" ? payload.source : "";
      const version = Number.isInteger(payload?.version) ? payload.version : -1;
      const result = compileSource(source, version);
      postResponse(id, true, result);
      return;
    }

    if (type === "run") {
      await ensureCompilerInitialized();
      const result = await runLastSuccessfulProgram();
      postResponse(id, true, result);
      return;
    }

    if (type === "lsp.hover") {
      await ensureCompilerInitialized();
      const source = typeof payload?.source === "string" ? payload.source : "";
      const version = Number.isInteger(payload?.version) ? payload.version : -1;
      const line = toNonNegativeInteger(payload?.line);
      const column = toNonNegativeInteger(payload?.column);
      const hover = getLspHover(source, version, line, column);
      postResponse(id, true, hover);
      return;
    }

    if (type === "lsp.definition") {
      await ensureCompilerInitialized();
      const source = typeof payload?.source === "string" ? payload.source : "";
      const version = Number.isInteger(payload?.version) ? payload.version : -1;
      const line = toNonNegativeInteger(payload?.line);
      const column = toNonNegativeInteger(payload?.column);
      const definition = getLspDefinition(source, version, line, column);
      postResponse(id, true, definition);
      return;
    }

    if (type === "lsp.semanticTokens") {
      await ensureCompilerInitialized();
      const source = typeof payload?.source === "string" ? payload.source : "";
      const version = Number.isInteger(payload?.version) ? payload.version : -1;
      const tokens = getLspSemanticTokens(source, version);
      postResponse(id, true, tokens);
      return;
    }

    postResponse(id, false, null, `Unknown worker message type '${type}'.`);
  } catch (error) {
    const message =
      error instanceof Error
        ? [error.message, error.stack].filter((part) => typeof part === "string" && part.length > 0).join("\n")
        : String(error);
    postResponse(id, false, null, message);
  }
};
