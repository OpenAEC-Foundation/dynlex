import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{allocationLimit:32*1024*1024});
assert.equal(result.output,'Farm execution shares editor mark layers, maps Unicode and CRLF, isolates main-file execution from imports, and clears stale ranges without losing search or colors.\n');
assert.equal(result.requests.length,0);
assert.equal(result.allocations.live,0);
console.log('Farm action and condition annotations share the standard editor, preserve semantic/search layers, and release every allocation.');
