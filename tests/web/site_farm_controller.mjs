import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const {output,requests,cancelled,allocations}=await recordWebsiteProgram(process.argv[2],{
  cancelRequest:()=>true,allocationLimit:32*1024*1024
});
assert.equal(output,'The production Farm link opens its declarative UI, loads the shared editor, advances, plays, resets and returns to the gallery.\n');
assert.equal(requests.length,4,'Opening loads the scene once; playing schedules a deadline; Stop and leaving each cancel a role compilation');
assert.equal(requests[0].service,'fetch');
assert.deepEqual(requests[0].payload,{url:'assets/games/farm/program.json'});
assert.equal(requests[1].service,'timer');
assert.deepEqual(requests[1].payload,{milliseconds:500});
assert.equal(requests[2].service,'compiler');
assert.equal(requests[2].payload.entry,'farm.dl');
assert.equal(requests[2].payload.traced,true);
assert.equal(requests[3].service,'compiler');
assert.deepEqual(cancelled,[2,3,4,1],'Reset stops the deadline; Stop and leaving cancel compilation; close cancels the pending scene load');
assert.equal(allocations.live,0,'Closing Farm must release its document, editor, options and pending requests.');
console.log('The actual website dispatcher opens and operates Farm without an unregistered action.');
