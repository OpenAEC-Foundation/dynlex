const decoder = new TextDecoder();

function copiedBytes(exports, pointerFunction, lengthFunction) {
  const pointer = pointerFunction();
  const length = lengthFunction();
  return new Uint8Array(exports.memory.buffer, pointer, length).slice();
}

async function instantiateTranslator(source) {
  if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
    const bytes = source instanceof ArrayBuffer
      ? source
      : source.buffer.slice(source.byteOffset, source.byteOffset + source.byteLength);
    return WebAssembly.instantiate(bytes);
  }
  const response = await fetch(source);
  if (!response.ok) {
    throw new Error("WGSL translator could not be loaded");
  }
  return WebAssembly.instantiateStreaming(response);
}

export async function createWgslTranslator(
  source = "/compiler/dynlex_wgsl_translator.wasm"
) {
  const instantiated = await instantiateTranslator(source);
  const exports = instantiated.instance.exports;
  for (const name of [
    "memory",
    "dynlex_wgsl_allocate",
    "dynlex_wgsl_deallocate",
    "dynlex_wgsl_translate",
    "dynlex_wgsl_result_pointer",
    "dynlex_wgsl_result_length",
    "dynlex_wgsl_error_pointer",
    "dynlex_wgsl_error_length"
  ]) {
    if (!(name in exports)) {
      throw new Error("WGSL translator has an invalid module interface");
    }
  }

  function translate(spirvBytes) {
    if (!(spirvBytes instanceof Uint8Array) || spirvBytes.byteLength === 0) {
      throw new Error("WGSL translation requires SPIR-V bytes");
    }
    const pointer = exports.dynlex_wgsl_allocate(spirvBytes.byteLength);
    try {
      new Uint8Array(exports.memory.buffer, pointer, spirvBytes.byteLength).set(spirvBytes);
      if (!exports.dynlex_wgsl_translate(pointer, spirvBytes.byteLength)) {
        const error = decoder.decode(copiedBytes(
          exports,
          exports.dynlex_wgsl_error_pointer,
          exports.dynlex_wgsl_error_length
        ));
        console.error("SPIR-V to WGSL translation failed", error);
        throw new Error("Shader translation failed. Check the browser log.");
      }
      const wgsl = decoder.decode(copiedBytes(
        exports,
        exports.dynlex_wgsl_result_pointer,
        exports.dynlex_wgsl_result_length
      ));
      if (wgsl.length === 0) {
        throw new Error("WGSL translator returned no shader source");
      }
      return wgsl;
    } finally {
      exports.dynlex_wgsl_deallocate(pointer, spirvBytes.byteLength);
    }
  }

  return Object.freeze({ translate });
}
