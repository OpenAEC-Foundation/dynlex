import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const {output,requests,cancelled}=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>true});
assert.equal(output,'Execution budgets begin after worker loading, terminate runaway programs and preserve cancellation, stale replies and recovery.\n');
assert.equal(requests.length,28);
assert.deepEqual(requests.flatMap((request,index)=>request.service==='timer'?[index+1]:[]),[3,4,7,8,11,12,15,16,19,22,23,28]);
assert.deepEqual(requests.filter(request=>request.service==='timer').map(request=>request.payload.milliseconds),
  [30000,5000,30000,5000,30000,5000,30000,5000,30000,30000,5000,30000]);
assert.deepEqual(cancelled,[3,4,7,6,8,11,10,15,14,18,19,22,23,27]);
console.log('Program deadline policy tests passed.');
