import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';

const output=path.resolve(process.argv[2]),bytes=await readFile(path.join(output,'lighting-data.wasm')),chunks=[];
const runtime=buildRuntimeImports(chunks,createRuntimeFilesystem(),inspectRuntimeWasmLayout(bytes));
const allocations=observeAllocations(runtime,32*1024*1024);
const {instance}=await WebAssembly.instantiate(bytes,runtime);
try {instance.exports.main();} catch(error) {throw new Error(chunks.join(''),{cause:error});}
assert.equal(allocations.live,0,'Farm lighting data retained its JSON values');
const data=JSON.parse(chunks.join(''));
assert.equal(data.length,480);
await writeFile(path.join(output,'lighting-data.json'),JSON.stringify(data));
console.log(`Farm reflection data: ${data.length} samples, ${allocations.peak} peak bytes, no retained allocations.`);

const sceneBytes=await readFile(path.join(output,'scene-data.wasm')),sceneChunks=[];
const stages=JSON.parse(await readFile(path.join(output,'stages.json'),'utf8'));
const linked=Object.fromEntries(Object.entries(stages).map(([name,stage])=>[name,{...stage.reflection,webgpu:stage.webgpu,webgl:stage.webgl}]));
const filesystem=createRuntimeFilesystem();
filesystem.files.set('stages.json',{data:new TextEncoder().encode(JSON.stringify(linked)),modificationTime:filesystem.lastTimestamp});
const sceneRuntime=buildRuntimeImports(sceneChunks,filesystem,inspectRuntimeWasmLayout(sceneBytes));
const sceneAllocations=observeAllocations(sceneRuntime,128*1024*1024);
const {instance:scene}=await WebAssembly.instantiate(sceneBytes,sceneRuntime);
try {scene.exports.main();} catch(error) {throw new Error(sceneChunks.join(''),{cause:error});}
assert.equal(sceneAllocations.live,0,'Farm renderer data retained its scene or graph');
const sceneData=JSON.parse(sceneChunks.join(''));
assert.equal(sceneData.program.passes.length,52);
assert(sceneData.instances.length/44>550);
assert(sceneData.draws.length/7>0);assert(sceneData.shadowDraws.length/7>0);
await writeFile(path.join(output,'scene-data.json'),JSON.stringify(sceneData));
console.log(`Farm scene: ${sceneData.instances.length/44} instances, ${sceneData.draws.length/7} draws, ${sceneAllocations.peak} peak bytes, no retained allocations.`);
