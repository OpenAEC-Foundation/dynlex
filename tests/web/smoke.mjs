import path from "node:path";
import { pathToFileURL } from "node:url";

function toNonNegativeInteger(value) {
  const number = Number.isFinite(Number(value)) ? Math.trunc(Number(value)) : 0;
  return number < 0 ? 0 : number;
}

function readUtf8(memory, pointer, length) {
  const bytes = new Uint8Array(memory.buffer);
  const start = toNonNegativeInteger(pointer);
  const maxLength = Math.max(0, Math.min(bytes.length - start, toNonNegativeInteger(length)));
  return new TextDecoder().decode(bytes.subarray(start, start + maxLength));
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
      output += String(Number(args[argIndex++] ?? 0));
      i += 1;
      continue;
    }

    output += `%${next}`;
    argIndex += 1;
    i += 1;
  }
  return output;
}

function buildRuntimeImports(importSpecs, stdoutChunks) {
  const memory = new WebAssembly.Memory({ initial: 256, maximum: 2048 });
  const indirectFunctionTable = new WebAssembly.Table({ initial: 64, maximum: 2048, element: "anyfunc" });
  const stackPointer = new WebAssembly.Global({ value: "i32", mutable: true }, 8 * 1024 * 1024);
  const memoryBase = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
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
      return Math.min(Number(left), Number(right));
    },
    fmax(left, right) {
      return Math.max(Number(left), Number(right));
    },
    pow(left, right) {
      return Math.pow(Number(left), Number(right));
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

const wasmBytes = Uint8Array.from(Buffer.from(wasmBase64, "base64"));
const wasmModule = await WebAssembly.compile(wasmBytes);
const importSpecs = WebAssembly.Module.imports(wasmModule);
const stdoutChunks = [];
const runtimeImports = buildRuntimeImports(importSpecs, stdoutChunks);
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

console.log(`DynLex web smoke passed (status=${status}, wasmLength=${wasmLength})`);
