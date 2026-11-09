import { createCompilerServices } from '/compiler/services.js';
import { WorkerConnection } from '/compiler/client.js';

const require = (condition, message) => { if (!condition) throw new Error(message); };

export async function verifyWorkers() {
  const provider = createCompilerServices(new URL('/compiler/', location.href), new URL('/sdk-sources.json', location.href));
  const deadline = new AbortController();
  const signal = deadline.signal;
  let phase = 'initial compile';
  const timer = setTimeout(() => deadline.abort(new Error(`Worker test timed out during ${phase}`)), 30000);
  const compile = source => provider.services.compiler({ operation: 'compile', target: 'wasm',
    entry: 'main.dl', traced: false, sources: { 'main.dl': source } }, { signal });
  try {
    const delayed = new WorkerConnection(new URL('/__tests__/delayed-worker.mjs', location.href));
    try {
      require((await delayed.request('echo', 'first', { signal })) === 'first',
        'A worker lost a request while its module dependencies initialized');
    } finally { delayed.close(); }
    const result = await compile('import lib/std.dl\nprint line 42\n');
    require(result.status === 0, JSON.stringify(result));
    const chunks = [];
    phase = 'first execution';
    await provider.services.program({ artifact: result.artifact, invocation: 1, outputLimit: 1024 },
      { signal, emit: event => chunks.push(event.output) });
    require(chunks.join('') === '42\n', 'The isolated program lost its output');
    phase = 'SDK compile';
    const sdk = await compile('import packages/dynlex-ui/ui/declarations.dl\nwindow demo:\n    button "Test"\nprint line demo\'s count\n');
    require(sdk.status === 0, 'The compiler worker could not import the pinned UI SDK');
    await provider.services.compiler({ operation: 'release', artifact: sdk.artifact }, { signal });
    phase = 'invalid source';
    const invalid = await compile('unknown instruction\n');
    require(invalid.status !== 0 && !Object.hasOwn(invalid, 'artifact') && invalid.diagnostics.length > 0,
      'A failed compilation retained a runnable artifact');
    phase = 'infinite source';
    const infinite = await compile('import lib/std.dl\nloop while true:\n    ignore 1\n');
    require(infinite.status === 0, JSON.stringify(infinite));
    const stop = new AbortController();
    const running = provider.services.program({ artifact: infinite.artifact, invocation: 2, outputLimit: 1024 },
      { signal: stop.signal, emit() {} });
    let heartbeat = false;
    setTimeout(() => { heartbeat = true; stop.abort(); }, 150);
    let cancelled = false;
    try { await running; } catch (error) { cancelled = error.name === 'AbortError'; }
    require(heartbeat && cancelled, 'An infinite program blocked the browser or ignored Stop');
    phase = 'compile after Stop';
    const next = await compile('import lib/std.dl\nprint line 7\n');
    require(next.status === 0, 'Stopping a program damaged the compiler');
    await provider.services.compiler({ operation: 'release', artifact: next.artifact }, { signal });
    phase = 'language server';
    const source = 'import packages/dynlex-ui/ui/declarations.dl\nwindow demo:\n    button "Test"\nprint line demo\'s count\n';
    await provider.services.language({ operation: 'project', sources: { 'main.dl': source } }, { signal });
    const language = (method, params, id) => provider.services.language({ operation: 'exchange',
      message: { jsonrpc: '2.0', ...(id === undefined ? {} : { id }), method, params } }, { signal });
    const initialized = await language('initialize', { processId: null, rootUri: 'file:///workspace', capabilities: {} }, 1);
    require(initialized.find(message => message.id === 1)?.result?.capabilities !== undefined,
      'The embedded language server did not initialize');
    await language('initialized', {});
    const opened = await language('textDocument/didOpen', { textDocument: {
      uri: 'file:///workspace/main.dl', languageId: 'dynlex', version: 1, text: source } });
    require(!opened.some(message => message.method === 'textDocument/publishDiagnostics'
      && message.params.diagnostics.some(diagnostic => diagnostic.severity === 1)),
      'Language-server imports did not see the installed project dependencies');
    const tokens = await language('textDocument/semanticTokens/full', {
      textDocument: { uri: 'file:///workspace/main.dl' } }, 2);
    require(tokens.find(message => message.id === 2)?.result?.data?.length > 0,
      'The embedded language server returned no semantic tokens');
    await language('shutdown', null, 3); await language('exit', null);
    return { output: chunks.join(''), compilationMilliseconds: result.milliseconds, cancelled };
  } finally { clearTimeout(timer); provider.close(); }
}
