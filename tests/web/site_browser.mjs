import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { setTimeout as pause } from 'node:timers/promises';
import { verifySourceCompletion, verifySourcePaste, verifySourceDefinition, verifySourceHover } from './editor_feature_checks.mjs';
import {canvasControls} from './canvas_controls.mjs';

const directory = path.resolve(process.argv[2] || 'build/site');
const graphicsMode = process.argv[3] || 'system';
const assetBase = process.argv[4] || '/';
assert(['system', 'webgpu', 'webgl-no-adapter', 'webgl-no-api'].includes(graphicsMode));
assert(assetBase.startsWith('/') && assetBase.endsWith('/'));
const pagePaths = new Set(['/']);
for (const page of JSON.parse(await fs.readFile(path.join(directory, 'routes.json'), 'utf8'))) {
  pagePaths.add(page.path); pagePaths.add(page.path.slice(0, page.path.lastIndexOf('/') + 1));
}
const profile = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-ui-chrome-'));
await fs.mkdir('.cache', { recursive: true });
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json',
  '.wasm': 'application/wasm', '.ttf': 'font/ttf', '.webp': 'image/webp',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg' };
const server = http.createServer(async (request, response) => {
  try {
    if (request.url === '/__tests__/workers.mjs') {
      response.setHeader('Content-Type', 'text/javascript');
      response.end(await fs.readFile(new URL('./site_worker_checks.mjs', import.meta.url)));
      return;
    }
    if (request.url === '/__tests__/delayed-worker.mjs') {
      response.setHeader('Content-Type', 'text/javascript');
      response.end(await fs.readFile(new URL('./fixtures/delayed-worker.mjs', import.meta.url)));
      return;
    }
    const pathname = new URL(request.url, 'http://localhost').pathname;
    const asset = pathname.startsWith(assetBase);
    if (!asset && !pagePaths.has(pathname)) throw new Error('Unversioned asset request');
    const filename = path.resolve(directory, asset ? pathname.slice(assetBase.length) : '.' + pathname);
    if (!filename.startsWith(directory + '/') && filename !== directory) throw new Error('Path');
    const resolved = (await fs.stat(filename)).isDirectory() ? path.join(filename, 'index.html') : filename;
    response.setHeader('Content-Type', mime[path.extname(resolved)] || 'application/octet-stream');
    response.end(await fs.readFile(resolved));
  } catch { response.writeHead(404); response.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const chrome = spawn(process.env.CHROME || 'google-chrome', [
  ...(process.env.DYNLEX_BACKGROUND_BROWSER ? ['--ozone-platform=x11'] : ['--headless=new']),
  ...(graphicsMode === 'webgpu' ? ['--enable-gpu', '--enable-unsafe-webgpu',
    '--enable-features=Vulkan', '--use-angle=vulkan', '--use-vulkan=swiftshader',
    '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] : []),
  ...(graphicsMode.startsWith('webgl-') ? ['--use-gl=angle', '--use-angle=swiftshader'] : []),
  '--mute-audio', '--no-first-run', '--disable-background-networking',
  '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'
], { detached: true, stdio: ['ignore', 'ignore', 'pipe'], env: { ...process.env, WAYLAND_DISPLAY: '', WAYLAND_SOCKET: '' } });
let stderr = ''; chrome.stderr.on('data', data => { stderr += data; });
let socket;
try {
  let port;
  for (let count = 0; count < 150; count++) {
    try { port = Number((await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]); break; }
    catch { await pause(100); }
  }
  assert(port, `Chrome did not start: ${stderr}`);
  const targets = await fetch(`http://127.0.0.1:${port}/json/list`).then(response => response.json());
  socket = new WebSocket(targets.find(target => target.type === 'page').webSocketDebuggerUrl);
  await new Promise(resolve => socket.addEventListener('open', resolve, { once: true }));
  let sequence = 0; const pending = new Map(), errors = [];
  let intercept;
  socket.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    if (message.id) {
      const operation = pending.get(message.id); pending.delete(message.id);
      if (message.error) operation.reject(new Error(message.error.message)); else operation.resolve(message.result);
    } else if (message.method === 'Fetch.requestPaused') {
      intercept(message.params).catch(error => errors.push(String(error)));
    } else if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails);
    else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
      errors.push(message.params.args.map(arg => arg.description || arg.value));
    }
  });
  const command = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async expression => {
    const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  const waitFor = async expression => {
    for (let count = 0; count < 300; count++) {
      assert.equal(errors.length, 0, JSON.stringify(errors));
      if (await evaluate(expression)) return;
      await pause(100);
    }
    const screenshot = await command('Page.captureScreenshot', { format: 'png' });
    await fs.writeFile(`.cache/site-failed-${graphicsMode}.png`, Buffer.from(screenshot.data, 'base64'));
    throw new Error(`Timed out: ${expression}\n${await evaluate('JSON.stringify({text:document.body.innerText,drawn:window.siteProbe.texts.slice(-20)})')}\n${stderr}`);
  };
  await command('Runtime.enable'); await command('Page.enable');
  await command('Browser.setDownloadBehavior', { behavior: 'deny' });
  const manifest = await fs.readFile(new URL('../../metadata/release-manifest.txt', import.meta.url), 'utf8');
  const assets = manifest.split('\n').filter(line => line.startsWith('asset ')).map(line => ({
    name: line.split(/\s+/)[5], browser_download_url: 'https://downloads.invalid/' + line.split(/\s+/)[5]
  }));
  assets.push({ name: 'release-manifest.txt', browser_download_url: 'https://downloads.invalid/release-manifest.txt' });
  const release = { tag_name: 'v0.0-browser-test', body: `<!-- dynlex-release-manifest\n${manifest.trimEnd()}\n-->`, assets };
  const downloads = [];
  intercept = async ({ requestId, request }) => {
    const headers = [{ name: 'Access-Control-Allow-Origin', value: '*' }];
    let body;
    if (request.url.startsWith('https://api.github.com/')) {
      headers.push({ name: 'Content-Type', value: 'application/json' }); body = JSON.stringify(release);
    } else {
      downloads.push(request.url); body = 'Test package; downloads are denied in this private browser.\n';
      headers.push({ name: 'Content-Disposition', value: 'attachment; filename="test-package.deb"' });
    }
    await command('Fetch.fulfillRequest', { requestId, responseCode: 200, responseHeaders: headers,
      body: Buffer.from(body).toString('base64') });
  };
  await command('Fetch.enable', { patterns: [{ urlPattern: 'https://api.github.com/*' }, { urlPattern: 'https://downloads.invalid/*' }] });
  await command('Emulation.setUserAgentOverride', {
    userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/140.0.0.0 Safari/537.36',
    userAgentMetadata: { brands: [{ brand: 'Chromium', version: '140' }],
      fullVersionList: [{ brand: 'Chromium', version: '140.0.0.0' }], platform: 'Linux',
      platformVersion: '6.0.0', architecture: 'x86', model: '', mobile: false, bitness: '64', wow64: false }
  });
  await command('Page.addScriptToEvaluateOnNewDocument', { source: await fs.readFile(new URL('./fixtures/ui-color-probe.js', import.meta.url), 'utf8') });
  await command('Page.addScriptToEvaluateOnNewDocument', { source: `
    window.siteProbe = { paints: 0, api: '', texts: [], visible: [], diagnostics: [], completions: [], compilations: [], builds: [], downloads: [], programOutput: '', pictures: [] };
    const instantiate = WebAssembly.instantiate.bind(WebAssembly);
    WebAssembly.instantiate = function(bytes, imports, ...args) {
      const env = imports?.env;
      if (env?.ui_text_mask) {
        const begin = env.ui_begin, mask = env.ui_text_mask;
        const decoder = new TextDecoder();
        const picture = env.ui_open_picture;
        env.ui_open_picture = (surface,view,image,done,pointer,count) => {
          window.siteProbe.pictures.push(decoder.decode(new Uint8Array(env.__linear_memory.buffer,pointer,count)));
          return picture(surface,view,image,done,pointer,count);
        };
        env.ui_begin = (...args) => { window.siteProbe.visible = []; return begin(...args); };
        env.ui_text_mask = (font, pointer, count, ...args) => {
          const result = mask(font, pointer, count, ...args);
          if (result) window.siteProbe.visible.push(decoder.decode(new Uint8Array(env.__linear_memory.buffer, pointer, count)));
          return result;
        };
      }
      return instantiate(bytes, imports, ...args);
    };
    const blobs = new Map();
    const objectUrl = URL.createObjectURL.bind(URL), revokeUrl = URL.revokeObjectURL.bind(URL);
    URL.createObjectURL = blob => { const url = objectUrl(blob); blobs.set(url, blob); return url; };
    URL.revokeObjectURL = url => { blobs.delete(url); revokeUrl(url); };
    const download = HTMLAnchorElement.prototype.click;
    HTMLAnchorElement.prototype.click = function(...args) {
      const record = { name: this.download, href: this.href };
      window.siteProbe.downloads.push(record);
      if (blobs.has(this.href)) blobs.get(this.href).text().then(content => { record.content = content; });
      return download.apply(this, args);
    };
    const BrowserWorker = window.Worker;
    window.Worker = class extends BrowserWorker {
      postMessage(message, ...args) {
        if (message.type === 'compile') window.siteProbe.compilations.push(message.payload.entry);
        return super.postMessage(message, ...args);
      }
      constructor(...args) {
        super(...args);
        this.addEventListener('message', ({ data }) => {
          if (data.event?.output) window.siteProbe.programOutput += data.event.output;
          if (data.result?.status !== undefined) window.siteProbe.builds.push({
            status: data.result.status, stages: Object.keys(data.result.stages || {})
          });
          if (!Array.isArray(data.result)) return;
          for (const message of data.result) if (Array.isArray(message.result?.items))
            window.siteProbe.completions.push(message.result.items);
          for (const message of data.result) if (message.method === 'textDocument/publishDiagnostics')
            window.siteProbe.diagnostics.push(message.params);
        });
      }
    };
    for (const prototype of [CanvasRenderingContext2D.prototype, OffscreenCanvasRenderingContext2D.prototype]) {
      const paintText = prototype.fillText;
      prototype.fillText = function(text, ...args) {
        window.siteProbe.texts.push(text); return paintText.call(this, text, ...args);
      };
    }
    const original = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(kind, ...args) {
      const result = original.call(this, kind, ...args);
      if (kind === 'webgpu' || kind === 'webgl2') window.siteProbe.api = kind;
      if (kind === 'webgl2' && result) {
        const clear = result.clear.bind(result);
        result.clear = bits => { window.siteProbe.paints++; clear(bits); };
      }
      return result;
    };
    if (typeof GPUQueue !== 'undefined') {
      const submit = GPUQueue.prototype.submit;
      GPUQueue.prototype.submit = function(...args) { window.siteProbe.paints++; return submit.apply(this,args); };
    }
    if (${JSON.stringify(graphicsMode)}.startsWith('webgl-')) {
      Object.defineProperty(Navigator.prototype, 'gpu', { configurable: true,
        get: () => ({ requestAdapter: async () => null }) });
    }
  ` });
  await command('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await command('Page.navigate', { url: `http://127.0.0.1:${server.address().port}/` });
  await waitFor('window.siteProbe?.paints > 2');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/painted-river.webp")');
  await pause(1800);
  assert.equal(errors.length,0,JSON.stringify(errors));
  const capture = async name => {
    const result = await command('Page.captureScreenshot', { format: 'png' });
    await fs.writeFile(`.cache/site-${name}-${graphicsMode}.png`,Buffer.from(result.data,'base64'));
  };
  await capture('desktop');
  console.log(JSON.stringify(await evaluate('({title:document.title,api:window.siteProbe.api,paints:window.siteProbe.paints})')));
  const {click,clickText,clickVisibleText}=canvasControls({command,evaluate,capture});
  await click(335,627); await pause(1500); await capture('terrain');
  await click(335,627); await pause(1500); await capture('nano');
  await click(800,22); await pause(700); await capture('challenges');
  await click(1115,22);
  await waitFor('location.pathname === "/wiki/index.html" && document.title === "DynLex Wiki"');
  await pause(1600); await capture('docs');
  const idle = await evaluate('window.siteProbe.paints');
  await pause(800);
  assert.equal(await evaluate('window.siteProbe.paints'),idle,'An unchanged documentation page kept repainting');
  console.log('Worker checks:', await evaluate(`import('/__tests__/workers.mjs').then(module => module.verifyWorkers(new URL(${JSON.stringify(assetBase)},location.href)))`));
  await click(1009,22);
  await waitFor('location.pathname === "/ide/index.html" && document.title === "DynLex Studio"');
  await waitFor('window.siteProbe.texts.includes("No problems found.")');
  await pause(500); await capture('ide');
  await click(237,287);
  await waitFor('window.siteProbe.texts.includes("Hello from DynLex.")');
  await click(120,480);
  const key = async (code,key,number,modifiers=0) => {
    await command('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:number,modifiers});
    await command('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:number,modifiers});
  };
  await verifySourceCompletion({command,evaluate,waitFor,key,capture,
    source:'import lib/std.dl\npri',prefix:'',name:'ide'});
  await verifySourcePaste({command,evaluate,waitFor,key,source:'import lib/std.dl\nprint line 7'});
  await verifySourceDefinition({command,evaluate,waitFor,key,capture,source:'import lib/std.dl\nprint line 7',line:1,column:0,filename:'string.dl',name:'ide'});
  await verifySourceHover({command,evaluate,waitFor,key,capture,source:'import lib/std.dl\nprint line 7',name:'ide'});
  await click(237,287);
  await waitFor('window.siteProbe.texts.includes("7")');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText', { text:'import lib/std.dl\nprint line ""' });
  await key('End','End',35,2); await key('ArrowLeft','ArrowLeft',37);
  await command('Input.imeSetComposition', { text:'とうきょう', selectionStart:5, selectionEnd:5 });
  await waitFor('window.siteProbe.visible.some(text => text.includes("とうきょう"))');
  await command('Input.insertText', { text:'東京' });
  await evaluate('window.siteProbe.programOutput = ""');
  await click(237,287);
  await waitFor('window.siteProbe.programOutput.includes("東京\\n")');
  await capture('ide-executed');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'unrecognized instruction'});
  await waitFor('window.siteProbe.texts.some(text => text.includes("This pattern couldn"))');
  const beforeValid = await evaluate('window.siteProbe.diagnostics.length');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'import lib/std.dl\nprint line "😀"'});
  await waitFor(`window.siteProbe.diagnostics.slice(${beforeValid}).some(result => result.diagnostics.length === 0)`);
  await waitFor(`window.colorProbe.some(item => item.text.includes('😀') && Math.abs(item.ink[0] - Math.pow((230/255+0.055)/1.055,2.4)) < 0.0001)`);
  await waitFor(`window.colorProbe.some(item => item.text.includes('print') && Math.abs(item.ink[0] - Math.pow((104/255+0.055)/1.055,2.4)) < 0.0001)`);
  await capture('ide-analysis');
  await pause(1000);
  const analyzed = await evaluate('window.siteProbe.paints');
  await pause(500);
  assert.equal(await evaluate('window.siteProbe.paints'), analyzed, 'An idle analyzed editor kept repainting');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'import lib/std.dl\nloop while true:\n    ignore 1'});
  await click(237,287); await pause(1800); await click(640,287);
  await waitFor('window.siteProbe.texts.includes("Stopped.")');
  await click(480,363);
  await waitFor('window.siteProbe.texts.some(text => text.startsWith("Shader compiled in"))');
  await capture('shader-default');
  await click(190,363);
  await click(1115,22);
  await waitFor('location.pathname === "/wiki/index.html"');
  await evaluate('history.back()'); await waitFor('location.pathname === "/ide/index.html"');
  await evaluate('history.back()'); await waitFor('location.pathname === "/wiki/index.html"');
  await evaluate('history.back()'); await waitFor('location.pathname === "/index.html" && location.hash === "#challenges"');
  await evaluate('history.back()'); await waitFor('location.pathname === "/"'); await pause(500);
  await click(905,22);
  await waitFor('location.hash === "#language"'); await pause(1000);
  await capture('vocabulary');
  await clickText('Remix'); await clickVisibleText('import lib/std.dl');
  await key('Enter','Enter',13,2);
  await command('Input.dispatchMouseEvent',{type:'mouseWheel',x:1100,y:700,deltaX:0,deltaY:300});
  await waitFor('window.siteProbe.texts.includes("The sketch is alive.")');
  await click(905,22); await pause(1000); await clickText('Define'); await clickVisibleText('import lib/std.dl');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'import lib/std.dl\nprint line "Retained definition"'});
  await clickText('Remix'); await clickText('Define'); await clickText('Run vocabulary ▶');
  await command('Input.dispatchMouseEvent',{type:'mouseWheel',x:1100,y:700,deltaX:0,deltaY:300});
  await waitFor('window.siteProbe.texts.includes("Retained definition")');
  await capture('vocabulary-executed');
  await click(60,37); await waitFor('location.pathname === "/index.html"'); await pause(700);
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:600,y:650});
  await command('Input.dispatchMouseEvent',{type:'mouseWheel',x:600,y:650,deltaX:0,deltaY:720});
  await pause(900); await capture('scrolled');
  await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 2, mobile: true });
  await pause(900); await capture('mobile');
  await command('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await pause(500); await click(1220,22);
  await waitFor('location.pathname === "/download.html" && window.siteProbe.texts.includes("DynLex for Linux")');
  for (let index = 0; index < 100 && downloads.length === 0; index++) await pause(50);
  assert.deepEqual(downloads, ['https://downloads.invalid/dynlex-linux-x64.deb']);
  await capture('download');
  const linkedSource = 'import lib/std.dl\nprint line "Shared link ran."\n';
  const entryUrl = '/ide/index.html?' + new URLSearchParams({
    code64: Buffer.from(linkedSource).toString('base64url'), name: 'linked.dl', autorun: 'true'
  });
  await command('Page.navigate', { url: `http://127.0.0.1:${server.address().port}${entryUrl}` });
  await waitFor('window.siteProbe?.texts.includes("Shared link ran.")');
  await waitFor('window.siteProbe.diagnostics.some(result => result.uri.endsWith("/linked.dl") && result.diagnostics.length === 0)');
  const renamedUrl = '/ide/index.html?' + new URLSearchParams({
    code: 'import lib/std.dl\nprint line "Renamed source ran."\n', name: 'renamed.dl', autorun: '1'
  });
  await evaluate(`history.pushState(null, '', ${JSON.stringify(renamedUrl)}); dispatchEvent(new PopStateEvent('popstate'))`);
  await waitFor('window.siteProbe.texts.includes("Renamed source ran.")');
  await waitFor('window.siteProbe.diagnostics.some(result => result.uri.endsWith("/renamed.dl") && result.diagnostics.length === 0)');
  await click(1000,287);
  await waitFor('window.siteProbe.downloads.some(item => item.name === "renamed.dl" && item.href.startsWith("blob:"))');
  await capture('ide-linked');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'abcd'});
  await key('Home','Home',36,2);
  await key('ArrowRight','ArrowRight',39); await key('ArrowRight','ArrowRight',39);
  const replacementUrl = '/ide/index.html?' + new URLSearchParams({
    code: '#😀\nimport lib/std.dl\nprint line "Replacement source"\n', name: 'caret.dl'
  });
  await evaluate(`history.pushState(null, '', ${JSON.stringify(replacementUrl)}); dispatchEvent(new PopStateEvent('popstate'))`);
  await waitFor('window.siteProbe.diagnostics.some(result => result.uri.endsWith("/caret.dl") && result.diagnostics.length === 0)');
  await capture('ide-replaced');
  const compilations = await evaluate('window.siteProbe.compilations.length');
  await click(1115,22); await waitFor('location.pathname === "/wiki/index.html"');
  await evaluate('history.back()'); await waitFor('location.pathname === "/ide/index.html"');
  await pause(700);
  assert.equal(await evaluate('window.siteProbe.compilations.length'), compilations,
    'History navigation repeated the source link autorun');
  const shaderSource = 'import lib/shader.dl\nset the fragment color with a red channel of 0.2, a green channel of 0.4, a blue channel of 0.8 and an alpha channel of 1.0\n';
  const shaderUrl = '/ide/?' + new URLSearchParams({ mode: 'shader', name: 'preview.dl', code: shaderSource });
  await command('Page.navigate', { url: `http://127.0.0.1:${server.address().port}${shaderUrl}` });
  await waitFor('window.siteProbe?.texts.some(text => text.startsWith("Shader compiled in"))');
  await waitFor('window.siteProbe.diagnostics.some(result => result.uri.endsWith("/preview.dl") && result.diagnostics.length === 0)');
  await capture('shader-inline');
  await pause(1200);
  const staticPaints = await evaluate('window.siteProbe.paints');
  await pause(500);
  assert.equal(await evaluate('window.siteProbe.paints'), staticPaints,
    'A shader without a time input kept repainting');
  await click(190,363);
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText', { text: 'import lib/std.dl\nprint line "Retained program tab"' });
  await click(480,363);
  await click(1000,287);
  await waitFor('window.siteProbe.downloads.some(item => item.name === "preview.dl" && item.content !== undefined)');
  assert.equal(await evaluate('window.siteProbe.downloads.at(-1).content'), shaderSource,
    'Switching editor tabs replaced the shader source');
  const previewImage = async () => (await command('Page.captureScreenshot', { format: 'png',
    clip: { x: 680, y: 418, width: 528, height: 320, scale: 1 } })).data;
  const successfulPreview = await previewImage();
  await evaluate('window.siteProbe.texts.length = 0');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText', { text: 'unknown shader instruction' });
  await click(237,287);
  await waitFor('window.siteProbe.texts.includes("Compilation failed.")');
  await capture('shader-failed-edit');
  assert.equal(await previewImage(), successfulPreview, 'A failed shader edit replaced the last successful preview');
  await evaluate('window.siteProbe.texts.length = 0');
  await click(120,480); await key('KeyA','a',65,2);
  await command('Input.insertText', { text: shaderSource });
  await click(237,287);
  await click(750,783);
  await waitFor('window.siteProbe.texts.some(text => text.startsWith("Shader compiled in"))');
  await waitFor('window.siteProbe.visible.includes("Resume preview")');
  await click(750,783);
  await waitFor('window.siteProbe.visible.includes("Pause preview")');
  for (const scene of ['event-horizon', 'endless-terrain', 'nano-choreography']) {
    const url = '/ide/index.html?' + new URLSearchParams({ mode: 'shader', scene });
    await evaluate(`window.siteProbe.texts.length = 0; history.pushState(null, '', ${JSON.stringify(url)}); dispatchEvent(new PopStateEvent('popstate'))`);
    await waitFor(`window.siteProbe.texts.includes(${JSON.stringify(scene + '.dl')}) && window.siteProbe.texts.some(text => text.startsWith("Shader compiled in"))`);
    await waitFor(`window.siteProbe.diagnostics.some(result => result.uri.endsWith(${JSON.stringify('/' + scene + '.dl')}) && result.diagnostics.length === 0)`);
    assert.deepEqual(await evaluate('window.siteProbe.builds.at(-1).stages'),
      scene === 'event-horizon' ? ['fragment'] : ['fragment', 'vertex']);
    await pause(500); await capture('shader-' + scene);
  }
  assert.equal(errors.length,0,JSON.stringify(errors));
  console.log('Site background preview passed.');
} finally {
  socket?.close();
  if (chrome.exitCode === null) {
    const stopped = new Promise(resolve => chrome.once('exit', resolve));
    process.kill(-chrome.pid, 'SIGTERM');
    await stopped;
  }
  await new Promise(resolve => server.close(resolve));
  await fs.rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
}
