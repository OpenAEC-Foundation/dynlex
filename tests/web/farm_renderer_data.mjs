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
