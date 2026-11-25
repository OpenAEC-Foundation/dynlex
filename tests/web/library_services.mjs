// Exercise the portable package adapter in a real, private browser without a compiler build.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {spawn, execFileSync} from 'node:child_process';
import {packageFromFolder} from '../../packages/manager.mjs';

execFileSync('google-chrome', ['--version'], {stdio: 'ignore'});
const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'dynlex-library-browser-'));
const packages = [], encoder = new TextEncoder();
let version = 0, resolveResult, rejectResult, chrome, stderr = '';
const result = new Promise((resolve, reject) => { resolveResult = resolve; rejectResult = reject; });
const timer = setTimeout(() => rejectResult(new Error('Browser library services timed out\n' + stderr)), 45000);
const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://localhost');
    if (url.pathname === '/result') {
      let body = ''; for await (const chunk of request) body += chunk;
      const report = JSON.parse(body); response.end('ok');
      if (report.error) rejectResult(new Error(report.error)); else resolveResult(report);
    } else if (url.pathname === '/version') { version = 1; response.end('ok'); }
    else if (url.pathname === '/catalog.json') {
      response.setHeader('Content-Type', 'application/json');
      response.end(JSON.stringify({schema: 1, libraries: [{name: 'example', alias: 'ui', title: 'Example',
        source: {url: `/package-${version}.zip`, contentHash: packages[version].manifest.contentHash}}]}));
    } else if (/^\/package-[01]\.zip$/.test(url.pathname)) {
      response.end(await fs.readFile(path.join(directory, url.pathname.slice(1))));
    } else if (/^\/packages\/(archive|manager|browser)\.mjs$/.test(url.pathname)) {
      response.setHeader('Content-Type', 'text/javascript');
      response.end(await fs.readFile(new URL('../..' + url.pathname, import.meta.url)));
    } else if (url.pathname === '/') {
      response.setHeader('Content-Type', 'text/html');
      response.end(`<!doctype html><script type="module">
        import {createLibraryServices} from '/packages/browser.mjs';
        import {emptyProject} from '/packages/manager.mjs';
        const check=(condition,message)=>{if(!condition) throw new Error(message);};
        try {
          const signal=new AbortController().signal, options={signal};
          const first=createLibraryServices(new URL('/catalog.json',location).href);
          check((await first.libraries({operation:'catalog'},options)).libraries.length===1,'Catalog fetch failed');
          let installed=await first.libraries({operation:'add',project:emptyProject(),alias:'ui',source:{catalog:'example'}},options);
          check(installed.sources['.dynlex/packages/example/src/main.dl']==='answer means: 42','Archive installation failed');
          const second=createLibraryServices(new URL('/catalog.json',location).href);
          const restored=await second.libraries({operation:'restore',project:installed.project},options);
          check(JSON.stringify(restored)===JSON.stringify(installed),'IndexedDB restore changed the project');
          const files=await second.runtimeFiles(installed.project);
          check(JSON.stringify([...files['.dynlex/packages/example/assets/pixels.bin']])==='[0,255]','Binary assets changed');
          await fetch('/version');
          installed=await second.libraries({operation:'update',project:installed.project,alias:'ui'},options);
          check(installed.sources['.dynlex/packages/example/src/main.dl']==='answer means: 84','Update kept old source');
          const removed=await second.libraries({operation:'remove',project:installed.project,alias:'ui'},options);
          check(Object.keys(removed.project.lock.packages).length===0,'Remove kept unused packages');
          const direct=await second.libraries({operation:'add',project:emptyProject(),alias:'ui',url:new URL('/package-0.zip?token=test',location).href},options);
          check(direct.sources['.dynlex/packages/example/src/main.dl']==='answer means: 42','Archive URL query was lost');
          const folder=new Map(${JSON.stringify([
            ['dynlex-package.json', [...encoder.encode(JSON.stringify({schema: 1, name: 'local-example', files: ['main.dl'], exports: {'main.dl': 'main.dl'}}))]],
            ['main.dl', [...encoder.encode('local answer means: 21')]]
          ])}.map(([name,data])=>[name,new Uint8Array(data)]));
          const local=createLibraryServices(new URL('/catalog.json',location).href,{selectFolder:async()=>folder});
          const selected=await local.libraries({operation:'folder',project:emptyProject(),alias:'local'},options);
          check(selected.project.lock.dependencies.local==='local-example','Folder installation failed');
          const localRestore=await second.libraries({operation:'restore',project:selected.project},options);
          check(localRestore.sources['.dynlex/packages/local-example/main.dl']==='local answer means: 21','Local folder cache was lost');
          await fetch('/result',{method:'POST',body:JSON.stringify({passed:true})});
        } catch(error) {await fetch('/result',{method:'POST',body:JSON.stringify({error:error.stack})});}
      </script>`);
    } else { response.writeHead(404); response.end(); }
  } catch (error) {
    console.error(error); response.writeHead(500); response.end('An error occurred. Check the log.'); rejectResult(error);
  }
});
try {
  for (const [index, answer] of [42, 84].entries()) {
    const folder = path.join(directory, `source-${index}`); await fs.mkdir(folder);
    const package_ = await packageFromFolder(new Map([
      ['dynlex-package.json', encoder.encode(JSON.stringify({schema: 1, name: 'example', files: ['src', 'assets'], exports: {'*': 'src/*'}}))],
      ['src/main.dl', encoder.encode(`answer means: ${answer}`)], ['assets/pixels.bin', new Uint8Array([0, 255])]
    ]), `revision-${index}`);
    packages.push(package_);
    for (const [name, bytes] of package_.files) {
      const filename = path.join(folder, name); await fs.mkdir(path.dirname(filename), {recursive: true}); await fs.writeFile(filename, bytes);
    }
    execFileSync('python3', ['-c', 'import pathlib,sys,zipfile\nroot=pathlib.Path(sys.argv[1])\nwith zipfile.ZipFile(sys.argv[2],"w",compression=zipfile.ZIP_DEFLATED) as archive:\n for file in sorted(root.rglob("*")):\n  if file.is_file(): archive.write(file,file.relative_to(root).as_posix())', folder, path.join(directory, `package-${index}.zip`)]);
  }
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  chrome = spawn('google-chrome', ['--headless', '--disable-gpu', '--mute-audio', '--disable-extensions', '--no-first-run',
    '--disable-background-networking', `--user-data-dir=${path.join(directory, 'profile')}`, `http://127.0.0.1:${server.address().port}/`],
  {stdio: ['ignore', 'ignore', 'pipe']});
  chrome.on('error', rejectResult); chrome.stderr.on('data', chunk => { stderr += chunk; });
  assert.equal((await result).passed, true);
  console.log('Browser libraries: catalog, ZIP, IndexedDB restore, binary assets, update, remove, signed URLs and local folders passed.');
} finally {
  clearTimeout(timer);
  if (chrome && chrome.exitCode === null) { chrome.kill(); await new Promise(resolve => chrome.once('close', resolve)); }
  await new Promise(resolve => server.close(resolve));
  await fs.rm(directory, {recursive: true, force: true});
}
