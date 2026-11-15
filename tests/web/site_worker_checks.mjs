const require = (condition, message) => { if (!condition) throw new Error(message); };

export async function verifyWorkers(base = new URL('/', location.href)) {
  const [{ createCompilerServices }, { WorkerConnection }] = await Promise.all([
    import(new URL('compiler/services.js', base)), import(new URL('compiler/client.js', base))
  ]);
  const provider = createCompilerServices(new URL('compiler/', base), new URL('sdk-sources.json', base));
  const deadline = new AbortController();
  const signal = deadline.signal;
  let phase = 'initial compile';
  const timer = setTimeout(() => deadline.abort(new Error(`Worker test timed out during ${phase}`)), 30000);
  const compile = source => provider.services.compiler({ operation: 'compile', target: 'cpu',
    entry: 'main.dl', traced: false, sources: { 'main.dl': source } }, { signal });
  try {
    const delayed = new WorkerConnection(new URL('/__tests__/delayed-worker.mjs', location.href));
    try {
      require((await delayed.request('echo', 'first', { signal })) === 'first',
        'A worker lost a request while its module dependencies initialized');
    } finally { delayed.close(); }
    const result = await compile('import lib/std.dl\nprint line 42\n');
    require(result.status === 0, JSON.stringify(result));
    const chunks = [], events = [];
    phase = 'first execution';
    await provider.services.program({ artifact: result.artifact, invocation: 1, outputLimit: 1024 },
      { signal, emit: event => { events.push(event.type); if(event.type==='program-output') chunks.push(event.output); } });
    require(chunks.join('') === '42\n', 'The isolated program lost its output');
    require(events[0]==='program-started' && events.filter(type=>type==='program-started').length===1,
      'A program did not announce its execution boundary before output');
    phase = 'output limit';
    const verbose = await compile('import lib/std.dl\nprint line "Too much output."\n');
    require(verbose.status === 0, JSON.stringify(verbose));
    let outputFailure;
    try {
      await provider.services.program({ artifact: verbose.artifact, invocation: 2, outputLimit: 4 },
        {signal,emit(){}});
    } catch (error) { outputFailure=error.message; }
    require(outputFailure==='An error occurred. Check the log.',
      'The output budget did not terminate execution with a generic client error');
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
    const running = provider.services.program({ artifact: infinite.artifact, invocation: 3, outputLimit: 1024 },
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
    phase = 'shader language profile';
    await provider.services.language({ operation: 'reset' }, { signal });
    const shaderSource = 'import lib/shader.dl\nset the fragment color with a red channel of 0.2, a green channel of 0.4, a blue channel of 0.8 and an alpha channel of 1.0\n';
    await provider.services.language({ operation: 'project', sources: { 'preview.dl': shaderSource } }, { signal });
    await language('initialize', { processId: null, rootUri: 'file:///workspace', capabilities: {},
      initializationOptions: { dynlex: { analysisProfiles: [{ target: 'spirv', shaderStage: 'fragment' }] } } }, 1);
    await language('initialized', {});
    const shaderOpened = await language('textDocument/didOpen', { textDocument: {
      uri: 'file:///workspace/preview.dl', languageId: 'dynlex', version: 1, text: shaderSource } });
    require(!shaderOpened.some(message => message.method === 'textDocument/publishDiagnostics'
      && message.params.diagnostics.some(diagnostic => diagnostic.severity === 1)),
      'Resetting the language service did not install the shader analysis profile');
    const shader = await provider.services.compiler({ operation: 'compile', target: 'shader', entry: 'preview.dl',
      stages: ['fragment'], sources: { 'preview.dl': shaderSource } }, { signal });
    require(shader.status === 0 && shader.stages.fragment.webgpu.includes('@fragment')
      && shader.stages.fragment.webgl.includes('void main'), 'The compiler did not return both shader backends');
    return { output: chunks.join(''), compilationMilliseconds: result.milliseconds, cancelled };
  } finally { clearTimeout(timer); provider.close(); }
}
