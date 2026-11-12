import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { setTimeout as pause } from 'node:timers/promises';
import { verifySourceCompletion, verifySourcePaste, verifySourceDefinition, verifySourceHover, verifySourceInspection, verifySourceFixes, verifySourceCallColor, verifySourceCallColors, verifySourceNavigationAppearance } from './editor_feature_checks.mjs';
import {canvasControls} from './canvas_controls.mjs';

const directory = path.resolve(process.argv[2] || 'build/site');
const graphicsMode = process.argv[3] || 'system';
const resumeOnly = process.argv[4] === 'resume';
assert(['system', 'webgpu', 'webgl-no-adapter', 'webgl-no-api'].includes(graphicsMode));
const profile = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-ui-chrome-'));
await fs.mkdir('.cache', { recursive: true });
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json',
  '.wasm': 'application/wasm', '.ttf': 'font/ttf', '.webp': 'image/webp',
  '.mp3': 'audio/mpeg', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg' };
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
    const filename = path.resolve(directory, '.' + new URL(request.url, 'http://localhost').pathname);
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
  if(resumeOnly) {
    const progress={version:7,games:{river:{updatedAt:Date.now(),data:{source:'this is invalid river code',solved:true,coding:{
      passenger:{count:1,seen:['loaded']},deliveries:{count:3,seen:['SHEEP','WOLF','HAY']},rescue:{count:1,seen:['solved']}
    }}}},lastGame:'river'};
    await command('Page.addScriptToEvaluateOnNewDocument',{source:`if(location.protocol==='http:') localStorage.setItem('dynlex.games.guest',${JSON.stringify(JSON.stringify(progress))});`});
  }
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
    window.siteProbe = { paints: 0, api: '', texts: [], visible: [], diagnostics: [], completions: [], compilations: [], builds: [], downloads: [], programOutput: '', pictures: [], marks: [], requests: [], contexts: [], sources: [], decoded: 0, frames: new Set() };
    const scheduleFrame = requestAnimationFrame.bind(window), cancelFrame = cancelAnimationFrame.bind(window);
    window.requestAnimationFrame = callback => {
      let id = scheduleFrame(time => { window.siteProbe.frames.delete(id); callback(time); });
      window.siteProbe.frames.add(id); return id;
    };
    window.cancelAnimationFrame = id => { window.siteProbe.frames.delete(id); cancelFrame(id); };
    const BrowserAudio = window.AudioContext;
    window.AudioContext = class extends BrowserAudio {
      constructor(...args) { super(...args); window.siteProbe.contexts.push(this); }
      decodeAudioData(...args) { return super.decodeAudioData(...args).then(buffer => { window.siteProbe.decoded++; return buffer; }); }
      createBufferSource() {
        const source = super.createBufferSource(), record = { source, started: false, ended: false, stopped: false };
        window.siteProbe.sources.push(record);
        const start = source.start.bind(source), stop = source.stop.bind(source);
        source.start = (...args) => { record.started = true; return start(...args); };
        source.stop = (...args) => { record.stopped = true; return stop(...args); };
        source.addEventListener('ended', () => { record.ended = true; });
        return source;
      }
    };
    const instantiate = WebAssembly.instantiate.bind(WebAssembly);
    WebAssembly.instantiate = function(bytes, imports, ...args) {
      const env = imports?.env;
      if (env?.ui_text_mask) {
        const begin = env.ui_begin, mask = env.ui_text_mask, triangles = env.ui_triangles;
        const decoder = new TextDecoder();
        const request = env.ui_request;
        env.ui_request = (surface,name,pointer,count) => {
          const bytes = new Uint8Array(env.__linear_memory.buffer); let end=name;
          while(bytes[end]) end++;
          const service=decoder.decode(bytes.subarray(name,end));
          if(service==='audio' || service==='timer') window.siteProbe.requests.push({service,payload:JSON.parse(decoder.decode(bytes.subarray(pointer,pointer+count)))});
          return request(surface,name,pointer,count);
        };
        const picture = env.ui_open_picture;
        env.ui_open_picture = (surface,view,image,done,pointer,count) => {
          window.siteProbe.pictures.push(decoder.decode(new Uint8Array(env.__linear_memory.buffer,pointer,count)));
          return picture(surface,view,image,done,pointer,count);
        };
        env.ui_begin = (...args) => { window.siteProbe.visible = []; window.siteProbe.marks = []; return begin(...args); };
        env.ui_text_mask = (font, pointer, count, ...args) => {
          const result = mask(font, pointer, count, ...args);
          if (result) {
            const caption = decoder.decode(new Uint8Array(env.__linear_memory.buffer, pointer, count));
            window.siteProbe.visible.push(caption);
          }
          return result;
        };
        env.ui_triangles = (surface,pointer,count,texture,x,y,width,height) => {
          if(!texture && count>0) {
            const vertices=new Float32Array(env.__linear_memory.buffer,pointer,count*8);
            const linear=value=>value<=10.31475?value/3294.6:Math.pow((value/255+0.055)/1.055,2.4);
            const palettes=[[104,153,178],[80,153,114],[196,153,66],[201,88,63]];
            if(Math.abs(vertices[7]-100/255)<0.0001 && palettes.some(ink=>ink.every((value,index)=>Math.abs(linear(value)-vertices[4+index])<0.0001)))
              window.siteProbe.marks.push({x:vertices[0],y:vertices[1]});
          }
          return triangles(surface,pointer,count,texture,x,y,width,height);
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
  await command('Emulation.setDeviceMetricsOverride', resumeOnly?{width:390,height:844,deviceScaleFactor:2,mobile:true}:{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await command('Page.navigate', { url: `http://127.0.0.1:${server.address().port}/${resumeOnly?'#challenges':''}` });
  await waitFor('window.siteProbe?.paints > 2');
  await waitFor('window.siteProbe.pictures.includes("assets/games/river/painted-river.webp")');
  await pause(1800);
  assert.equal(errors.length,0,JSON.stringify(errors));
  const capture = async name => {
    const result = await command('Page.captureScreenshot', { format: 'png' });
    await fs.writeFile(`.cache/site-${name}-${graphicsMode}.png`,Buffer.from(result.data,'base64'));
  };
  await capture(resumeOnly?'river-resume-start':'desktop');
  console.log(JSON.stringify(await evaluate('({title:document.title,api:window.siteProbe.api,paints:window.siteProbe.paints})')));
  if(graphicsMode !== 'system') assert.equal(await evaluate('window.siteProbe.api'),graphicsMode === 'webgpu' ? 'webgpu' : 'webgl2');
  const {click,clickText,textVisible,wheel}=canvasControls({command,evaluate,capture});
  const key = async (code,key,number,modifiers=0) => {
    await command('Input.dispatchKeyEvent',{type:'keyDown',code,key,windowsVirtualKeyCode:number,modifiers});
    await command('Input.dispatchKeyEvent',{type:'keyUp',code,key,windowsVirtualKeyCode:number,modifiers});
  };
  const source = await fs.readFile(new URL('./fixtures/river_solution.dl',import.meta.url),'utf8');
  const plan = source.slice(source.indexOf('\n')+1).trimStart();
  let saved;
  if(!resumeOnly) {
  await click(800,22); await pause(800);
  await clickText('Open the river challenge →');
  await waitFor('window.siteProbe.pictures.length >= 10');
  await waitFor('window.siteProbe.decoded === 7 && window.siteProbe.contexts[0].state === "running"');
  await waitFor('window.siteProbe.texts.includes("THE RIVER / WAITING FOR A PLAN")');
  // Observe actual text draw colors, rather than just the server's token reply.
  await waitFor(`window.colorProbe.some(item => item.text.includes('# The official names') && Math.abs(item.ink[0] - Math.pow((149/255+0.055)/1.055,2.4)) < 0.0001)`);
  await verifySourceCallColor({waitFor,text:'get'});
  await pause(500); await capture('river-ready');
  const idle = await evaluate('window.siteProbe.paints');
  await pause(500);
  assert.equal(await evaluate('window.siteProbe.paints'),idle,'An idle River scene kept repainting');
  await clickText('Audio on ♪');
  await waitFor(textVisible('Audio off ♪'));
  assert(await evaluate('window.siteProbe.requests.some(item => item.service === "audio" && item.payload.operation === "state" && item.payload.volume === 0)'));
  await clickText('Audio off ♪');
  const audible = 'window.siteProbe.requests.filter(item => item.service === "audio" && item.payload.operation === "state").slice(-2).some(item => item.payload.volume > 0.2)';
  await waitFor(audible);
  const hiddenTab = await command('Target.createTarget',{url:'about:blank'});
  await waitFor('document.hidden');
  await waitFor('window.siteProbe.requests.filter(item => item.service === "audio" && item.payload.operation === "state").slice(-2).every(item => item.payload.volume === 0)');
  await pause(250);
  const hiddenPaints = await evaluate('window.siteProbe.paints');
  await pause(300);
  assert.equal(await evaluate('window.siteProbe.paints'),hiddenPaints,'The hidden surface kept polling after its visibility event');
  await command('Target.closeTarget',{targetId:hiddenTab.targetId});
  await waitFor('!document.hidden');
  await waitFor(audible);
  await clickText('One instruction');
  await waitFor('window.siteProbe.texts.includes("Play ▶")');
  await waitFor('window.siteProbe.marks.length > 0');
  await pause(400); await capture('river-step');
  await waitFor('window.siteProbe.frames.size === 0');
  const paused = await evaluate('window.siteProbe.paints');
  await pause(400);
  assert.equal(await evaluate('window.siteProbe.paints'),paused,'A stepped River scene kept repainting');
  await clickText('1× speed'); await clickText('2× speed');
  await clickText('Play ▶');
  await wheel(300);
  await waitFor('window.siteProbe.texts.includes("Try again.")');
  await capture('river-failure');
  // The source editor is the only code surface in this active view. Click its
  // starter comment, select the document, and type through normalized input.
  await clickText('# The official names are: sheep, wolf, and hay.');
  await verifySourceCompletion({command,evaluate,waitFor,key,capture,
    source:'get t',prefix:'import lib/river_challenge.dl\n\n',name:'river'});
  await verifySourcePaste({command,evaluate,waitFor,key,source:plan});
  await verifySourceNavigationAppearance({waitFor,disabled:true});
  await verifySourceDefinition({command,evaluate,waitFor,key,capture,source:plan,line:1,column:4,filename:'river_challenge.dl',name:'river'});
  await verifySourceHover({command,evaluate,waitFor,key,capture,source:plan,name:'river'});
  await verifySourceInspection({command,evaluate,waitFor,key,capture,source:plan,name:'river'});
  await verifySourceFixes({command,evaluate,waitFor,key,capture,source:plan,name:'river'});
  await verifySourceCallColors({command,waitFor,key,capture,source:plan,name:'river'});
  await pause(100);
  assert.equal(await evaluate('window.siteProbe.marks.length'),0,'Editing retained execution marks from another snapshot');
  await clickText('Run plan ▶');
  await waitFor('window.siteProbe.texts.includes("THE RIVER / EVERYONE MADE IT")');
  await capture('river-solved');
  await wheel(320);
  await waitFor('window.siteProbe.texts.includes("3 / 3")');
  await clickText('4× speed');
  await clickText('Replay ▶');
  await waitFor(textVisible('Pause Ⅱ'));
  await waitFor('window.siteProbe.sources.some(item => item.started && !item.stopped && !item.ended && !item.source.loop && item.source.playbackRate.value === 1 && Math.abs(item.source.buffer.duration - 1) < 0.1)');
  await clickText('Pause Ⅱ',0);
  await pause(300);
  assert(await evaluate('window.siteProbe.sources.some(item => item.started && !item.stopped && !item.ended && item.source.playbackRate.value === 0)'),'Pause did not reach its audio voice: '+await evaluate('JSON.stringify(window.siteProbe.sources.map(item => ({started:item.started,stopped:item.stopped,ended:item.ended,rate:item.source.playbackRate.value,loop:item.source.loop})))'));
  await waitFor('window.siteProbe.frames.size === 0');
  const stopped = await evaluate('window.siteProbe.paints');
  await pause(400);
  assert.equal(await evaluate('window.siteProbe.paints'),stopped,'A paused River animation kept repainting');
  await clickText('Reset scene ↺');
  await clickText('1× speed'); await clickText('2× speed');
  await clickText('ferry the sheep');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:'this is invalid river code'});
  await key('Enter','Enter',13,2);
  await waitFor('window.siteProbe.texts.some(text => text.includes("/workspace/river.dl:1:1:"))');
  await capture('river-diagnostics');
  await command('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:2,mobile:true});
  await pause(600);
  await clickText('4× speed');
  await waitFor(textVisible('1× speed'));
  await capture('river-mobile');
  assert.equal(await evaluate('document.querySelector("canvas").getBoundingClientRect().width'),390);
  await clickText('← Choose a challenge');
  await pause(150);
  assert(await evaluate('window.siteProbe.requests.filter(item => item.service === "audio" && item.payload.operation === "state").slice(-2).every(item => item.payload.volume === 0)'),'Leaving the challenge did not silence its background sounds');
  await waitFor(textVisible('01 / THE RIVER'));
  saved=await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest"))');
  assert.equal(saved.version,7);assert.equal(saved.lastGame,'river');
  assert.equal(saved.games.river.data.source,'this is invalid river code');
  assert.equal(saved.games.river.data.solved,true);
  assert.deepEqual(saved.games.river.data.coding,{passenger:{count:1,seen:['loaded']},
    deliveries:{count:3,seen:['SHEEP','WOLF','HAY']},rescue:{count:1,seen:['solved']}});
  assert(Number.isSafeInteger(saved.games.river.updatedAt)&&Math.abs(Date.now()-saved.games.river.updatedAt)<180000);
  await command('Page.reload',{ignoreCache:true});
  } else saved=await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest"))');
  await waitFor('window.siteProbe?.paints > 2');
  await clickText('Continue your game →');
  await clickText('this is invalid river code');
  // Mobile places the world and goals below the source editor. Reveal them
  // before checking their painted state rather than waiting above the fold.
  await clickText('THE RIVER / WAITING FOR A PLAN');
  await clickText('3 / 3');
  if(resumeOnly) {
    await clickText('← Choose a challenge');
    await waitFor(textVisible('01 / THE RIVER'));
  }
  assert.deepEqual(await evaluate('JSON.parse(localStorage.getItem("dynlex.games.guest"))'),saved,'Reload or Resume rewrote stored progress');
  assert.equal(errors.length,0,JSON.stringify(errors));
  console.log(resumeOnly?'DynLex River mobile resume passed: saved source, world, goal counters and unchanged storage.':
    'DynLex River browser interaction passed: step, failure, edit, solve, replay, pause, reset, diagnostics, marks, audio, mobile layout, local saves and resume after reload.');
} finally {
  socket?.close();
  if (chrome.exitCode === null) {
    const stopped = new Promise(resolve => chrome.once('exit', resolve));
    process.kill(-chrome.pid, 'SIGTERM'); await stopped;
  }
  await new Promise(resolve => server.close(resolve));
  await fs.rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
}
