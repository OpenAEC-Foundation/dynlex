import assert from "node:assert/strict";
import { createServer } from "node:http";
import { command, evaluate, waitFor, siteOrigin, closeBrowserSession } from "./browser_test_driver.mjs";

// (module (func (export "main") (loop br 0))) exercises the runtime deadline
// without depending on the compiler to produce a deliberately runaway role.
const runawayWasm = [0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,7,8,1,4,109,97,105,110,0,0,10,9,1,7,0,3,64,12,0,11,11];

// Delay the real worker module on its first cold load. Loading code must not
// consume the execution budget intended to stop an unbounded user program.
let delayWorker = true;
const server = createServer(async (request, response) => {
  if (new URL(request.url, siteOrigin).pathname === "/farm-program-worker.js" && delayWorker) {
    delayWorker = false;
    await new Promise(resolve => setTimeout(resolve, 6000));
  }
  try {
    const upstream = await fetch(new URL(request.url, siteOrigin));
    response.writeHead(upstream.status, { "Content-Type": upstream.headers.get("content-type") });
    response.end(Buffer.from(await upstream.arrayBuffer()));
  } catch (error) { console.error(error); response.writeHead(500); response.end(); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
try {
  await command("Page.navigate", { url: `http://127.0.0.1:${server.address().port}/farm.css` });
  await waitFor("location.pathname === '/farm.css'", "preparation fixture");
  await evaluate(`(async () => {
    document.body.textContent='Farm role preparation test';
    const compiler=new Worker('/compiler/compiler-worker.js',{type:'module'});
    let nextId=0;const pending=new Map();
    compiler.onmessage=({data})=>{const p=pending.get(data.id);pending.delete(data.id);data.ok?p.resolve(data.payload):p.reject(new Error(data.error));};
    const call=(type,payload={})=>new Promise((resolve,reject)=>{const id=++nextId;pending.set(id,{resolve,reject});compiler.postMessage({id,type,payload});});
    await call('init');
    const {createRoles}=await import('/farm-roles.js');
    const {FARM_PREFIX}=await import('/farm-program.js');
    window.compiledRoles=[];
    const roles=createRoles();
    roles.push({name:'Custom role',source:'to take a breath:\\n    wait\\n\\nloop forever:\\n    take a breath'});
    for(const role of roles){
      const result=await call('compile',{source:FARM_PREFIX+role.source,version:compiledRoles.length+1});
      if(result.status!==0)throw new Error(JSON.stringify(result.diagnostics));
      compiledRoles.push({name:role.name,wasm:(await call('artifact')).wasm});
    }
    compiler.terminate();
  })()`);
  const coldLoad = await evaluate(`(async () => {
    const {buildFarmPlan}=await import('/farm-program.js');
    const start=performance.now();
    try{const plan=await buildFarmPlan(compiledRoles[0].wasm);return {elapsed:performance.now()-start,plan};}
    catch(error){return {error:error.message,elapsed:performance.now()-start};}
  })()`);
  assert.equal(coldLoad.error, undefined, `A slow worker download rejected a valid Lumberjack role: ${coldLoad.error}`);
  assert.ok(coldLoad.plan.some(instruction => instruction.key === "chop"));
  assert.ok(coldLoad.elapsed >= 5900, "The worker download was actually delayed beyond the execution budget");
  const counts = await evaluate(`(async () => {
    const {buildFarmPlan}=await import('/farm-program.js');
    const counts=[];
    for(const role of compiledRoles) counts.push({name:role.name,count:(await buildFarmPlan(role.wasm)).length});
    return counts;
  })()`);
  assert.ok(counts.every(role => role.count > 0));
  const runaway = await evaluate(`(async () => {
    const {buildFarmPlan}=await import('/farm-program.js');
    const wasm=new Uint8Array(${JSON.stringify(runawayWasm)});
    const start=performance.now();
    try{await buildFarmPlan(wasm);return {resolved:true};}
    catch(error){return {error:error.message,elapsed:performance.now()-start};}
  })()`);
  assert.match(runaway.error, /execution.*(?:limit|long)|(?:limit|long).*execut/i);
  assert.ok(runaway.elapsed >= 4900 && runaway.elapsed < 15000);
  const cancelled = await evaluate(`(async () => {
    const {buildFarmPlan}=await import('/farm-program.js');
    const wasm=new Uint8Array(${JSON.stringify(runawayWasm)});
    const controller=new AbortController(),start=performance.now();
    const preparing=buildFarmPlan(wasm,{signal:controller.signal});
    setTimeout(()=>controller.abort(),100);
    try{await preparing;return {resolved:true};}
    catch(error){return {name:error.name,elapsed:performance.now()-start};}
  })()`);
  assert.equal(cancelled.name, "AbortError");
  assert.ok(cancelled.elapsed < 2000, "Cancelling an assignment must terminate its worker immediately");
  const recovered = await evaluate("import('/farm-program.js').then(async ({buildFarmPlan})=>(await buildFarmPlan(compiledRoles[0].wasm)).length)");
  assert.ok(recovered > 0, "A terminated role must not prevent the next assignment");
  console.log("Slow worker startup, the example and a custom role, runaway execution termination, cancellation and subsequent assignment passed.");
} finally {
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
  await closeBrowserSession();
}
