import { buildRuntimeImports, createRuntimeFilesystem, inspectRuntimeWasmLayout } from './runtimeImports.js';
import { ProgramDebugger } from './debugger.js';
import { readCString } from './runtimeFilesystem.js';

let debuggerSession;

// Every invocation runs in its own disposable worker. Termination interrupts a
// program even when it never yields, prints, or reaches an instrumented line.
self.onmessage = async ({ data: { id, type, payload } }) => {
  if (type === 'debug') {
    try {
      if (!debuggerSession) throw new Error('The program has no active debugger');
      self.postMessage({ id, result: debuggerSession.command(payload) });
    } catch (error) {
      console.error('DynLex debugger:', error);
      self.postMessage({ id, error: 'An error occurred. Check the log.' });
    }
    return;
  }
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
    const filesystem = createRuntimeFilesystem();
    for (const [name, data] of Object.entries(payload.files)) {
      filesystem.files.set(name, {data, modificationTime: filesystem.lastTimestamp});
      let slash = name.lastIndexOf('/');
      while (slash >= 0) {
        filesystem.directories.set(name.slice(0, slash), filesystem.lastTimestamp);
        slash = name.lastIndexOf('/', slash - 1);
      }
    }
    const runtime = buildRuntimeImports(output, filesystem, inspectRuntimeWasmLayout(payload.wasm));
    if (payload.debugged) {
      if (typeof WebAssembly.Suspending !== 'function' || typeof WebAssembly.promising !== 'function')
        throw new Error('Browser debugging requires WebAssembly JavaScript Promise Integration');
      debuggerSession = new ProgramDebugger(runtime.env.__linear_memory, readCString,
        event => self.postMessage({ id, event }), flush, payload.breakpoints);
      runtime.env.dynlex_debug_enter = (name, location) => debuggerSession.enter(name, location);
      runtime.env.dynlex_debug_leave = () => debuggerSession.leave();
      runtime.env.dynlex_debug_location = new WebAssembly.Suspending((metadata, addresses) => debuggerSession.point(metadata, addresses));
    }
    const start = performance.now();
    const { instance } = await WebAssembly.instantiate(payload.wasm, runtime);
    self.postMessage({ id, event: { type: 'program-started' } });
    if (payload.debugged) await WebAssembly.promising(instance.exports.main)();
    else instance.exports.main();
    flush();
    self.postMessage({ id, result: { milliseconds: performance.now() - start } });
  } catch (error) {
    flush();
    console.error('DynLex program:', error);
    self.postMessage({ id, error: 'An error occurred. Check the log.' });
  } finally {
    debuggerSession = undefined;
  }
};
self.postMessage({ ready: true });
