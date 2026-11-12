// Compare portable DynLex model data with independently captured authored snapshots.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';

const cases=JSON.parse(await fs.readFile(new URL('./farm_art_cases.json',import.meta.url),'utf8'));
const input=process.argv[2];
let actual;
if(input.endsWith('.wasm')) {
  const bytes=await fs.readFile(input),chunks=[];
  const filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.json',{data:new TextEncoder().encode(JSON.stringify(cases)),modificationTime:filesystem.lastTimestamp});
  const runtime=buildRuntimeImports(chunks,filesystem,inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,32*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);
  instance.exports.main();
  actual=JSON.parse(chunks.join(''));
  assert.equal(allocation.live,0,'The Farm art library or its instances retained allocations');
  console.log('Farm art allocations:',allocation.peak,'peak bytes; zero retained');
} else actual=JSON.parse(await fs.readFile(input,'utf8'));

const captured=JSON.parse(await fs.readFile(new URL('./models/farm_art_expected.json',import.meta.url),'utf8'));
assert.match(captured.sourceRevision,/^[a-f0-9]{40}$/);
assert.deepEqual(Object.keys(captured.models).sort(),cases.map(entry=>entry.id).sort(),'Every model needs an independent authored snapshot');
function compare(actual,expected,location) {
  if(typeof expected==='number') {
    assert.equal(typeof actual,'number',location);
    assert(Math.abs(actual-expected)<0.00001,`${location}: ${actual} != ${expected}`);
  } else if(Array.isArray(expected)) {
    assert(Array.isArray(actual),location);assert.equal(actual.length,expected.length,location);
    expected.forEach((value,index)=>compare(actual[index],value,`${location}[${index}]`));
  } else if(expected!==null&&typeof expected==='object') {
    assert(actual!==null&&typeof actual==='object',location);
    assert.deepEqual(Object.keys(actual).sort(),Object.keys(expected).sort(),location);
    for(const key of Object.keys(expected))compare(actual[key],expected[key],`${location}.${key}`);
  } else assert.equal(actual,expected,location);
}
assert.deepEqual(Object.keys(actual).sort(),cases.map(entry=>entry.id).sort(),'Every authored case must execute');
let meshes=0;
for(const entry of cases) {
  const expected=captured.models[entry.id];compare(actual[entry.id],expected,entry.id);meshes+=expected.meshes.length;
}
console.log(`PASS Farm art: ${cases.length} models and ${meshes} meshes preserve geometry, transforms, materials, bounds and named pivots.`);
