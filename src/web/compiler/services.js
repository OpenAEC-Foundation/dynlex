import { WorkerConnection } from './client.js';
import { createLibraryServices } from './packages/browser.mjs';

// Browser compiler capabilities. Project UI, results, diagnostics and execution
// policy are controlled by the DynLex application calling these services.
export function createCompilerServices(base, dependencyUrl) {
  let dependencies, nextArtifact = 1;
  const compilers = new Map(), runners = new Map(), artifacts = new Map();
  const libraries = createLibraryServices(new URL('../libraries/index.json', base).href);
  function connection(name) {
    if (!compilers.has(name)) compilers.set(name, new WorkerConnection(new URL('worker.js', base),
      () => compilers.delete(name)));
    return compilers.get(name);
  }
  async function sources() {
    dependencies ??= fetch(dependencyUrl).then(async response => {
      if (!response.ok) throw new Error(`Source dependencies: HTTP ${response.status}`);
      return response.json();
    }).catch(error => { dependencies = undefined; throw error; });
    return dependencies;
  }
  const services = {
    libraries: libraries.libraries,
    async compiler(payload, { signal }) {
      if (payload.operation === 'release') {
        if (!artifacts.delete(payload.artifact)) throw new Error('Unknown compiler artifact');
        return null;
      }
      if (payload.operation !== 'compile') throw new Error(`Unknown compiler operation: ${payload.operation}`);
      let target;
      if (payload.target === 'cpu') target = 'wasm';
      else if (payload.target === 'shader') target = 'shader';
      else throw new Error(`Unknown compiler target: ${payload.target}`);
      const result = await connection('build').request('compile', {
        sources: { ...await sources(), ...payload.sources }, entry: payload.entry,
        target, traced: payload.traced, debugged: payload.debugged, stages: payload.stages
      }, { signal });
      if (result.status === 0 && target === 'wasm') {
        let files = {};
        if (Object.hasOwn(payload.sources, 'dynlex.json')) {
          const manifest = JSON.parse(payload.sources['dynlex.json']);
          if (Object.keys(manifest.dependencies).length) files = await libraries.runtimeFiles({manifest, lock: JSON.parse(payload.sources['dynlex.lock'])});
        }
        result.artifact = nextArtifact++;
        artifacts.set(result.artifact, { wasm: result.wasm, files, debugged: payload.debugged === true });
        delete result.wasm;
      }
      return result;
    },
    async language(payload, { signal }) {
      if (payload.operation === 'reset') return connection('language').request('reset', {}, { signal });
      if (payload.operation === 'project') return connection('language').request('project',
        { sources: { ...await sources(), ...payload.sources } }, { signal });
      if (payload.operation === 'exchange') return connection('language').request('language',
        { message: payload.message }, { signal });
      throw new Error(`Unknown language operation: ${payload.operation}`);
    },
    async program(payload, { signal, emit }) {
      if (!artifacts.has(payload.artifact)) throw new Error('Unknown compiler artifact');
      const { wasm, files, debugged } = artifacts.get(payload.artifact); artifacts.delete(payload.artifact);
      if (runners.has(payload.invocation)) throw new Error('Program invocation is already running');
      const worker = new WorkerConnection(new URL('run-worker.js', base)); runners.set(payload.invocation, worker);
      try {
        return await worker.request('run', { wasm, files, outputLimit: payload.outputLimit,
          debugged, breakpoints: payload.breakpoints }, { signal,
          transfer: [...new Set([wasm.buffer, ...Object.values(files).map(data => data.buffer)])], event: event => emit({ ...event, invocation: payload.invocation }) });
      } finally { worker.close(); runners.delete(payload.invocation); }
    },
    async debugger(payload, { signal }) {
      signal?.throwIfAborted();
      const worker = runners.get(payload.invocation);
      if (!worker) throw new Error('Debugger invocation is not running');
      // Cancelling an inspection discards its UI reply; it must not kill the running program.
      const result = await worker.request('debug', payload);
      return payload.command === 'variables' ? { pause: payload.pause, reference: payload.reference, variables: result } : result;
    }
  };
  return { services, close() {
    for (const worker of compilers.values()) worker.close();
    for (const worker of runners.values()) worker.close();
    artifacts.clear();
  } };
}
