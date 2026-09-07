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

export async function createWgslTranslator(source) {
  const instantiated = await instantiateTranslator(source);
  const exports = instantiated.instance.exports;
  for (const name of [
    "memory",
    "dynlex_wgsl_allocate",
    "dynlex_wgsl_deallocate",
    "dynlex_wgsl_translate",
    "dynlex_glsl_translate",
    "dynlex_shader_translate",
    "dynlex_wgsl_result_pointer",
    "dynlex_wgsl_result_length",
    "dynlex_glsl_result_pointer",
    "dynlex_glsl_result_length",
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

  function translateGlsl(spirvBytes, stage) {
    if (!(spirvBytes instanceof Uint8Array) || spirvBytes.byteLength === 0) {
      throw new Error("GLSL translation requires SPIR-V bytes");
    }
    const stageCode = { vertex: 0, fragment: 1 }[stage];
    if (stageCode === undefined) {
      throw new Error("GLSL translation requires a vertex or fragment stage");
    }
    const pointer = exports.dynlex_wgsl_allocate(spirvBytes.byteLength);
    try {
      new Uint8Array(exports.memory.buffer, pointer, spirvBytes.byteLength).set(spirvBytes);
      if (!exports.dynlex_glsl_translate(pointer, spirvBytes.byteLength, stageCode)) {
        const error = decoder.decode(copiedBytes(
          exports,
          exports.dynlex_wgsl_error_pointer,
          exports.dynlex_wgsl_error_length
        ));
        console.error("SPIR-V to GLSL translation failed", error);
        throw new Error("Shader translation failed. Check the browser log.");
      }
      const glsl = decoder.decode(copiedBytes(
        exports,
        exports.dynlex_wgsl_result_pointer,
        exports.dynlex_wgsl_result_length
      ));
      if (glsl.length === 0) {
        throw new Error("GLSL translator returned no shader source");
      }
      return glsl;
    } finally {
      exports.dynlex_wgsl_deallocate(pointer, spirvBytes.byteLength);
    }
  }

  function translateBoth(spirvBytes, stage) {
    if (!(spirvBytes instanceof Uint8Array) || spirvBytes.byteLength === 0) {
      throw new Error("Shader translation requires SPIR-V bytes");
    }
    const stageCode = { vertex: 0, fragment: 1 }[stage];
    if (stageCode === undefined) {
      throw new Error("Shader translation requires a vertex or fragment stage");
    }
    const pointer = exports.dynlex_wgsl_allocate(spirvBytes.byteLength);
    try {
      new Uint8Array(exports.memory.buffer, pointer, spirvBytes.byteLength).set(spirvBytes);
      if (!exports.dynlex_shader_translate(pointer, spirvBytes.byteLength, stageCode)) {
        const error = decoder.decode(copiedBytes(
          exports,
          exports.dynlex_wgsl_error_pointer,
          exports.dynlex_wgsl_error_length
        ));
        console.error("SPIR-V shader translation failed", error);
        throw new Error("Shader translation failed. Check the browser log.");
      }
      return Object.freeze({
        webgpu: decoder.decode(copiedBytes(
          exports,
          exports.dynlex_wgsl_result_pointer,
          exports.dynlex_wgsl_result_length
        )),
        webgl: decoder.decode(copiedBytes(
          exports,
          exports.dynlex_glsl_result_pointer,
          exports.dynlex_glsl_result_length
        ))
      });
    } finally {
      exports.dynlex_wgsl_deallocate(pointer, spirvBytes.byteLength);
    }
  }

  return Object.freeze({ translate, translateGlsl, translateBoth });
}
