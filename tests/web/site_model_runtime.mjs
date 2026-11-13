// Pure website models must release their owned state on both compiler frontends.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout}
  from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';

const [file,expectation]=process.argv.slice(2);
const bytes=await fs.readFile(file),expected=await fs.readFile(expectation,'utf8');
const chunks=[],runtime=buildRuntimeImports(chunks,createRuntimeFilesystem(),inspectRuntimeWasmLayout(bytes));
const allocations=observeAllocations(runtime,32*1024*1024);
const {instance}=await WebAssembly.instantiate(bytes,runtime);
try {assert.equal(instance.exports.main(),0);} catch(error) {throw new Error(chunks.join(''),{cause:error});}
assert.equal(chunks.join(''),expected);
assert.equal(allocations.live,0,'Website model retained owned allocations');
console.log(`PASS model ownership: ${allocations.peak} peak bytes, zero retained`);
