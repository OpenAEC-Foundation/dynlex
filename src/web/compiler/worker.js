// The browser compiler protocol owns compiler state and artifacts, never UI.
const base = new URL('./', import.meta.url);
let compilerPromise, translatorPromise;

async function compiler() {
  compilerPromise ??= (async () => {
    const { default: create } = await import(new URL('dynlex_web.js', base));
    const module = await create({ locateFile: name => new URL(name, base).href });
    module.ccall('dynlex_web_init');
    return module;
  })();
  return compilerPromise;
}

async function translator() {
  translatorPromise ??= (async () => {
    const { createWgslTranslator } = await import(new URL('wgsl-translator.js', base));
    return createWgslTranslator(new URL('dynlex_wgsl_translator.wasm', base));
  })();
  return translatorPromise;
}

function decode(value) {
  return Uint8Array.from(atob(value), character => character.charCodeAt(0));
}

function feedback(module) {
  return {
    diagnostics: JSON.parse(module.ccall('dynlex_web_get_diagnostics_json', 'string')).diagnostics,
    compilerLog: JSON.parse(module.ccall('dynlex_web_get_compiler_log_json', 'string')).messages
  };
}

function checked(module, name, types, values) {
  if (module.ccall(name, 'number', types, values) !== 0)
    throw new Error(`${name}: ${JSON.stringify(feedback(module))}`);
}

async function handle(type, payload) {
  const module = await compiler();
  if (type === 'initialize') return null;
  if (type === 'reset') { module.ccall('dynlex_web_init'); return null; }
  if (type === 'project') {
    checked(module, 'dynlex_web_set_import_sources', ['string'], [JSON.stringify(payload.sources)]);
    return null;
  }
  if (type === 'language') {
    return JSON.parse(module.ccall('dynlex_web_lsp_exchange_json', 'string',
      ['string'], [JSON.stringify(payload.message)]));
  }
  if (type !== 'compile') throw new Error(`Unknown compiler operation: ${type}`);
  checked(module, 'dynlex_web_set_import_sources', ['string'], [JSON.stringify(payload.sources)]);
  checked(module, 'dynlex_web_set_entry_file', ['string'], [payload.entry]);
  const start = performance.now();
  if (payload.target === 'wasm') {
    const status = module.ccall(payload.traced ? 'dynlex_web_compile_and_emit_wasm_traced'
      : 'dynlex_web_compile_and_emit_wasm', 'number');
    const result = { status, milliseconds: performance.now() - start, ...feedback(module) };
    if (status === 3 || status === 4) throw new Error(JSON.stringify(result));
    if (status === 0) result.wasm = decode(module.ccall('dynlex_web_get_output_wasm_base64', 'string'));
    return result;
  }
  if (payload.target !== 'shader') throw new Error(`Unknown compiler target: ${payload.target}`);
  const translate = await translator();
  const stages = {};
  let milliseconds = 0;
  for (const stage of payload.stages) {
    const start = performance.now();
    const status = module.ccall('dynlex_web_compile_and_emit_shader_spirv', 'number', ['string'], [stage]);
    milliseconds += performance.now() - start;
    const result = { status, milliseconds, ...feedback(module) };
    if (status === 3 || status === 4) throw new Error(JSON.stringify(result));
    if (status !== 0) return result;
    const bytes = decode(module.ccall('dynlex_web_get_output_shader_spirv_base64', 'string'));
    const translated = translate.translateBoth(bytes, stage);
    stages[stage] = { webgpu: translated.webgpu, webgl: translated.webgl, glslBindings: translated.bindings, glslUniforms: translated.uniforms,
      ...JSON.parse(module.ccall('dynlex_web_get_shader_reflection_json', 'string')) };
  }
  return { status: 0, milliseconds, ...feedback(module), stages };
}

// Emscripten and the language server are mutable; requests execute in order.
let queue = Promise.resolve();
self.onmessage = ({ data: { id, type, payload } }) => {
  queue = queue.then(async () => {
    try {
      const result = await handle(type, payload);
      self.postMessage({ id, result }, result?.wasm ? [result.wasm.buffer] : []);
    } catch (error) {
      console.error('DynLex compiler:', error);
      self.postMessage({ id, fatal: true, error: 'An error occurred. Check the log.' });
    }
  });
};
self.postMessage({ ready: true });
