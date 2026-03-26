const compilerBasePath = "/compiler/";

const supportedEnvImports = new Set([
  "__linear_memory",
  "__stack_pointer",
  "__memory_base",
  "__table_base",
  "__indirect_function_table",
  "dynlex_print_string",
  "dynlex_print_i64",
  "calloc",
  "malloc",
  "free",
  "memcpy",
  "memmove",
  "memset",
  "strlen",
  "printf",
  "snprintf",
  "fmin",
  "fmax",
  "pow",
  "rand",
  "srand",
  "time"
]);

const state = {
  compilerModule: null,
  initialized: false,
  lastSuccessfulWasm: null,
  artifactVersion: 0
};

function postResponse(id, ok, payload, error) {
  self.postMessage({ id, ok, payload, error });
}

function toNumber(value) {
  if (typeof value === "bigint") {
    return Number(value);
  }
  return Number(value);
}

function toNonNegativeInteger(value) {
  const number = Number.isFinite(toNumber(value)) ? Math.trunc(toNumber(value)) : 0;
  return number < 0 ? 0 : number;
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

function compileSource(source) {
  const module = state.compilerModule;
  module.ccall("dynlex_web_set_main_source", null, ["string"], [source]);
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

function isSupportedImport(importSpec) {
  if (importSpec.module !== "env") {
    return false;
  }
  if (supportedEnvImports.has(importSpec.name)) {
    return true;
  }
  return importSpec.name.startsWith("GOT.");
}

function readCString(memory, pointer) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  let end = start;
  while (end < bytes.length && bytes[end] !== 0) {
    end += 1;
  }
  return new TextDecoder().decode(bytes.subarray(start, end));
}

function readUtf8(memory, pointer, length) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const maxLength = Math.max(0, Math.min(bytes.length - start, toNonNegativeInteger(length)));
  return new TextDecoder().decode(bytes.subarray(start, start + maxLength));
}

function buildFormatStringOutput(memory, formatText, args) {
  let argIndex = 0;
  let output = "";
  for (let i = 0; i < formatText.length; i += 1) {
    const char = formatText[i];
    if (char !== "%") {
      output += char;
      continue;
    }
    if (i + 1 >= formatText.length) {
      output += "%";
      break;
    }

    const next = formatText[i + 1];
    if (next === "%") {
      output += "%";
      i += 1;
      continue;
    }

    if (next === "." && formatText[i + 2] === "*" && formatText[i + 3] === "s") {
      const requestedLength = toNonNegativeInteger(args[argIndex++] ?? 0);
      const stringPointer = args[argIndex++] ?? 0;
      output += readUtf8(memory, stringPointer, requestedLength);
      i += 3;
      continue;
    }

    if (next === "l" && formatText[i + 2] === "d") {
      const value = args[argIndex++] ?? 0;
      output += String(value);
      i += 2;
      continue;
    }

    if (next === "s") {
      const stringPointer = args[argIndex++] ?? 0;
      output += readCString(memory, stringPointer);
      i += 1;
      continue;
    }

    if (next === "d" || next === "i") {
      output += String(toNonNegativeInteger(args[argIndex++] ?? 0));
      i += 1;
      continue;
    }

    if (next === "f" || next === "g") {
      output += String(toNumber(args[argIndex++] ?? 0));
      i += 1;
      continue;
    }

    output += `%${next}`;
    argIndex += 1;
    i += 1;
  }
  return output;
}

function writeCString(memory, pointer, text, limit) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const encoded = new TextEncoder().encode(text);
  const maxContent = limit > 0 ? Math.max(0, Math.min(limit - 1, encoded.length)) : 0;
  for (let i = 0; i < maxContent; i += 1) {
    bytes[start + i] = encoded[i];
  }
  if (limit > 0 && start + maxContent < bytes.length) {
    bytes[start + maxContent] = 0;
  }
}

function buildRuntimeImports(importSpecs, stdoutChunks) {
  const memory = new WebAssembly.Memory({ initial: 256, maximum: 2048 });
  const indirectFunctionTable = new WebAssembly.Table({ initial: 64, maximum: 2048, element: "anyfunc" });
  const stackPointer = new WebAssembly.Global({ value: "i32", mutable: true }, 8 * 1024 * 1024);
  const memoryBase = new WebAssembly.Global({ value: "i32", mutable: false }, 1024);
  const tableBase = new WebAssembly.Global({ value: "i32", mutable: false }, 0);

  let heapPointer = 12 * 1024 * 1024;
  let randomState = 0x12345678;

  function ensureMemoryForAddress(address) {
    const requiredAddress = toNonNegativeInteger(address);
    const currentSize = memory.buffer.byteLength;
    if (requiredAddress < currentSize) {
      return;
    }
    const pageSize = 65536;
    const requiredPages = Math.ceil((requiredAddress - currentSize + 1) / pageSize);
    if (requiredPages > 0) {
      memory.grow(requiredPages);
    }
  }

  function allocateBytes(byteCount, zeroFill) {
    const bytes = toNonNegativeInteger(byteCount);
    if (bytes === 0) {
      return heapPointer;
    }
    const alignedHeapPointer = (heapPointer + 7) & ~7;
    const endPointer = alignedHeapPointer + bytes;
    ensureMemoryForAddress(endPointer);
    if (zeroFill) {
      new Uint8Array(memory.buffer).fill(0, alignedHeapPointer, endPointer);
    }
    heapPointer = endPointer;
    return alignedHeapPointer;
  }

  const env = {
    __linear_memory: memory,
    __stack_pointer: stackPointer,
    __memory_base: memoryBase,
    __table_base: tableBase,
    __indirect_function_table: indirectFunctionTable,
    dynlex_print_string(pointer, length) {
      stdoutChunks.push(readUtf8(memory, pointer, length));
      return 0;
    },
    dynlex_print_i64(value) {
      stdoutChunks.push(String(value));
      return 0;
    },
    calloc(count, size) {
      return allocateBytes(toNonNegativeInteger(count) * toNonNegativeInteger(size), true);
    },
    malloc(size) {
      return allocateBytes(size, false);
    },
    free() {},
    memcpy(destination, source, length) {
      const bytes = new Uint8Array(memory.buffer);
      const dst = toNonNegativeInteger(destination);
      const src = toNonNegativeInteger(source);
      const len = toNonNegativeInteger(length);
      ensureMemoryForAddress(dst + len);
      bytes.set(bytes.subarray(src, src + len), dst);
      return dst;
    },
    memmove(destination, source, length) {
      const bytes = new Uint8Array(memory.buffer);
      const dst = toNonNegativeInteger(destination);
      const src = toNonNegativeInteger(source);
      const len = toNonNegativeInteger(length);
      ensureMemoryForAddress(dst + len);
      bytes.copyWithin(dst, src, src + len);
      return dst;
    },
    memset(destination, value, length) {
      const bytes = new Uint8Array(memory.buffer);
      const dst = toNonNegativeInteger(destination);
      const len = toNonNegativeInteger(length);
      ensureMemoryForAddress(dst + len);
      bytes.fill(toNonNegativeInteger(value) & 0xff, dst, dst + len);
      return dst;
    },
    strlen(pointer) {
      return readCString(memory, pointer).length;
    },
    printf(formatPointer, ...args) {
      const format = readCString(memory, formatPointer);
      const output = buildFormatStringOutput(memory, format, args);
      stdoutChunks.push(output);
      return output.length;
    },
    snprintf(bufferPointer, bufferSize, formatPointer, ...args) {
      const format = readCString(memory, formatPointer);
      const output = buildFormatStringOutput(memory, format, args);
      writeCString(memory, bufferPointer, output, toNonNegativeInteger(bufferSize));
      return output.length;
    },
    fmin(left, right) {
      return Math.min(toNumber(left), toNumber(right));
    },
    fmax(left, right) {
      return Math.max(toNumber(left), toNumber(right));
    },
    pow(left, right) {
      return Math.pow(toNumber(left), toNumber(right));
    },
    rand() {
      randomState = (1103515245 * randomState + 12345) & 0x7fffffff;
      return randomState;
    },
    srand(seed) {
      randomState = toNonNegativeInteger(seed) || 1;
    },
    time() {
      return Math.floor(Date.now() / 1000);
    }
  };

  for (const importSpec of importSpecs) {
    if (importSpec.module !== "env" || !importSpec.name.startsWith("GOT.")) {
      continue;
    }
    if (!(importSpec.name in env)) {
      env[importSpec.name] = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
    }
  }

  return { env };
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
  const unsupportedImports = imports.filter((importSpec) => !isSupportedImport(importSpec));
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
  const runtimeImports = buildRuntimeImports(imports, stdoutChunks);

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
// Phase 1 messages:
// - init -> initResult
// - compile { source } -> compileResult
// - run -> runResult
// Phase 2 extension point:
// - reserve "lsp.*" message namespace for richer editor features.
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
      const result = compileSource(source);
      postResponse(id, true, result);
      return;
    }

    if (type === "run") {
      await ensureCompilerInitialized();
      const result = await runLastSuccessfulProgram();
      postResponse(id, true, result);
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
