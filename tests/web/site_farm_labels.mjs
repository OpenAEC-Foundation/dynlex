import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const {output,shaderCalls,allocations}=await recordWebsiteProgram(process.argv[2],{
  cancelRequest:()=>true,allocationLimit:32*1024*1024
});
assert.equal(output,'Farm names retain identity through selection, Unicode renaming, dragging, new people, profile replacement and scene lifetime.\n');
assert.equal(shaderCalls.length,1);
assert.equal(shaderCalls[0].operation,'close');
assert.equal(shaderCalls[0].resource,42);
assert(shaderCalls[0].surface>0,'The scene was not attached to its test surface');
assert.equal(allocations.live,0,'Closing the Farm name layer retained UI, scene or document state');
console.log('Farm name selection, Unicode, drag suppression, replacement and ownership passed.');
