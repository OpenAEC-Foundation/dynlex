import {
  buildRuntimeImports,
  createRuntimeFilesystem,
  inspectRuntimeWasmLayout,
  isSupportedRuntimeImport,
  toNonNegativeInteger
} from "./runtimeImports.js";

const compilerBasePath = "/compiler/";
const compilerRevision = new URL(self.location.href).searchParams.get("revision");
if (!compilerRevision) {
  throw new Error("Compiler worker requires an artifact revision");
}

function compilerAssetUrl(path) {
  const url = new URL(`${compilerBasePath}${path}`, self.location.origin);
  url.searchParams.set("revision", compilerRevision);
  return url.href;
}

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

function parseCompilerJson(text, label) {
  if (typeof text !== "string" || text.length === 0) {
    throw new Error(`Compiler returned no ${label} JSON`);
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`Compiler returned invalid ${label} JSON`, { cause: error });
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

  const imported = await import(/* @vite-ignore */ compilerAssetUrl("dynlex_web.js"));
  const createModule = imported.default ?? imported;
  if (typeof createModule !== "function") {
    throw new Error("dynlex_web.js did not export a module factory.");
  }

  state.compilerModule = await createModule({
    locateFile(path) {
      return compilerAssetUrl(path);
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

function compilerFeedback(module) {
  const diagnosticsPayload = parseCompilerJson(
    module.ccall("dynlex_web_get_diagnostics_json", "string", [], []),
    "diagnostics"
  );
  const compilerLogPayload = parseCompilerJson(
    module.ccall("dynlex_web_get_compiler_log_json", "string", [], []),
    "log"
  );
  if (!Array.isArray(diagnosticsPayload.diagnostics) || !Array.isArray(compilerLogPayload.messages)) {
    throw new Error("Compiler returned malformed feedback");
  }
  return {
    diagnostics: diagnosticsPayload.diagnostics,
    compilerLog: compilerLogPayload.messages
  };
}

function compileSource(source, version) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const compilationStartedAt = performance.now();
  const status = module.ccall("dynlex_web_compile_and_emit_wasm", "number", [], []);
  const compilationMilliseconds = performance.now() - compilationStartedAt;

  const feedback = compilerFeedback(module);

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
    ...feedback,
    compilationMilliseconds,
    hasArtifact: !!state.lastSuccessfulWasm,
    artifactVersion: state.artifactVersion
  };
}

function compileShaderStage(source, version, stage) {
  const module = state.compilerModule;
  syncCompilerSource(source, version);
  const compilationStartedAt = performance.now();
  const status = module.ccall(
    "dynlex_web_compile_and_emit_shader_glsl",
    "number",
    ["string"],
    [stage]
  );
  const compilationMilliseconds = performance.now() - compilationStartedAt;
  const feedback = compilerFeedback(module);
  const glslSource = module.ccall("dynlex_web_get_output_shader_glsl", "string", [], []);
  const uniformPayload = parseCompilerJson(
    module.ccall("dynlex_web_get_shader_uniforms_json", "string", [], []),
    "shader uniform"
  );

  if (status === 0 && (typeof glslSource !== "string" || glslSource.length === 0)) {
    throw new Error("Successful shader compilation returned no WebGL source");
  }
  if (!Array.isArray(uniformPayload.uniforms)) {
    throw new Error("Successful shader compilation returned invalid uniform reflection");
  }

  return {
    status,
    ...feedback,
    compilationMilliseconds,
    glslSource: status === 0 ? glslSource : "",
    uniforms: status === 0 ? uniformPayload.uniforms : []
  };
}

function compileShaderSource(source, version, compileVertexStage) {
  const fragment = compileShaderStage(source, version, "fragment");
  if (fragment.status !== 0 || !compileVertexStage) {
    return {
      ...fragment,
      fragmentSource: fragment.glslSource,
      vertexSource: ""
    };
  }

  const vertex = compileShaderStage(source, version, "vertex");
  return {
    status: vertex.status,
    diagnostics: vertex.diagnostics,
    compilerLog: [...fragment.compilerLog, ...vertex.compilerLog],
    compilationMilliseconds: fragment.compilationMilliseconds + vertex.compilationMilliseconds,
    glslSource: fragment.glslSource,
    fragmentSource: fragment.glslSource,
    vertexSource: vertex.status === 0 ? vertex.glslSource : "",
    uniforms: fragment.uniforms
  };
}

function exchangeLsp(message) {
  if (!message || typeof message !== "object" || message.jsonrpc !== "2.0") {
    throw new Error("Worker received an invalid LSP JSON-RPC message");
  }
  const module = state.compilerModule;
  const messages = parseCompilerJson(
    module.ccall(
      "dynlex_web_lsp_exchange_json",
      "string",
      ["string"],
      [JSON.stringify(message)]
    ),
    "LSP exchange"
  );
  if (!Array.isArray(messages)) {
    throw new Error("Compiler returned malformed LSP exchange JSON");
  }
  return messages;
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
// - compile.shader { source, version } -> shaderCompileResult
// - run -> runResult
// - lsp.exchange { message } -> JSON-RPC messages emitted by the DynLex language server
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

    if (type === "compile.shader") {
      await ensureCompilerInitialized();
      const source = typeof payload?.source === "string" ? payload.source : "";
      const version = Number.isInteger(payload?.version) ? payload.version : -1;
      const compileVertexStage = payload?.renderer === true;
      const result = compileShaderSource(source, version, compileVertexStage);
      postResponse(id, true, result);
      return;
    }

    if (type === "run") {
      await ensureCompilerInitialized();
      const result = await runLastSuccessfulProgram();
      postResponse(id, true, result);
      return;
    }

    if (type === "lsp.exchange") {
      await ensureCompilerInitialized();
      postResponse(id, true, exchangeLsp(payload?.message));
      return;
    }

    postResponse(id, false, null, `Unknown worker message type '${type}'.`);
  } catch (error) {
    console.error("Compiler worker request failed", error);
    postResponse(id, false, null, "An error occurred. Check the browser log.");
  }
};
