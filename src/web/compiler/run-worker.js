import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from './runtimeImports.js';

// Every invocation runs in its own disposable worker. Termination interrupts a
// program even when it never yields, prints, or reaches an instrumented line.
self.onmessage = async ({ data: { id, payload } }) => {
  let buffered = '', length = 0;
  const flush = () => {
    if (buffered.length) { self.postMessage({ id, event: { type: 'program-output', output: buffered } }); buffered = ''; }
  };
  try {
    if (!Number.isSafeInteger(payload.outputLimit) || payload.outputLimit <= 0)
      throw new Error('Execution requires a positive output limit');
    const output = { push(chunk) {
      length += chunk.length;
      if (length > payload.outputLimit) throw new Error('The program exceeded its output limit');
      buffered += chunk;
      if (buffered.length >= 4096) flush();
    } };
    const runtime = buildRuntimeImports(output, createRuntimeFilesystem(), inspectRuntimeWasmLayout(payload.wasm));
    const start = performance.now();
    const { instance } = await WebAssembly.instantiate(payload.wasm, runtime);
    self.postMessage({ id, event: { type: 'program-started' } });
    instance.exports.main();
    flush();
    self.postMessage({ id, result: { milliseconds: performance.now() - start } });
  } catch (error) {
    flush();
    console.error('DynLex program:', error);
    self.postMessage({ id, error: 'An error occurred. Check the log.' });
  }
};
self.postMessage({ ready: true });
