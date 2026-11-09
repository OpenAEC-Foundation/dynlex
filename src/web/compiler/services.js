import { WorkerConnection } from './client.js';

// Browser compiler capabilities. Project UI, results, diagnostics and execution
// policy are controlled by the DynLex application calling these services.
export function createCompilerServices(base, dependencyUrl) {
  let dependencies, nextArtifact = 1;
  const compilers = new Map(), runners = new Set(), artifacts = new Map();
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
    async compiler(payload, { signal }) {
      if (payload.operation === 'release') {
        if (!artifacts.delete(payload.artifact)) throw new Error('Unknown compiler artifact');
        return null;
      }
      if (payload.operation !== 'compile') throw new Error(`Unknown compiler operation: ${payload.operation}`);
      const result = await connection('build').request('compile', {
        sources: { ...await sources(), ...payload.sources }, entry: payload.entry,
        target: payload.target, traced: payload.traced, stages: payload.stages
      }, { signal });
      if (result.status === 0 && payload.target === 'wasm') {
        result.artifact = nextArtifact++; artifacts.set(result.artifact, result.wasm); delete result.wasm;
      }
      return result;
    },
    async language(payload, { signal }) {
      if (payload.operation === 'project') return connection('language').request('project',
        { sources: { ...await sources(), ...payload.sources } }, { signal });
      if (payload.operation === 'exchange') return connection('language').request('language',
        { message: payload.message }, { signal });
      throw new Error(`Unknown language operation: ${payload.operation}`);
    },
    async program(payload, { signal, emit }) {
      if (!artifacts.has(payload.artifact)) throw new Error('Unknown compiler artifact');
      const wasm = artifacts.get(payload.artifact); artifacts.delete(payload.artifact);
      const worker = new WorkerConnection(new URL('run-worker.js', base)); runners.add(worker);
      try {
        return await worker.request('run', { wasm, outputLimit: payload.outputLimit }, { signal,
          transfer: [wasm.buffer], event: event => emit({ type: 'program-output',
            invocation: payload.invocation, output: event.output }) });
      } finally { worker.close(); runners.delete(worker); }
    }
  };
  return { services, close() {
    for (const worker of compilers.values()) worker.close();
    for (const worker of runners) worker.close();
    artifacts.clear();
  } };
}
