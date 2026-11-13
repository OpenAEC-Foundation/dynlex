import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{
  allocationLimit:32*1024*1024,
  synchronousService(service,payload) {
    if(service==='clock') return {milliseconds:42};
    if(service==='storage' && payload.action==='set') return null;
    throw new Error('Unexpected profile fixture service: '+service);
  }
});
assert.equal(result.output,'Switching profiles during a captured Farm gesture clears the old world and draft without writing either store.\n');
assert.deepEqual(result.calls,[],'A profile switch must not save the previous Farm into the new store.');
assert.deepEqual(result.requests,[]);
assert.deepEqual(result.shaderCalls.filter(call=>call.operation==='close').map(call=>call.resource),[77]);
assert.equal(result.allocations.live,0,'Switching profiles retained a Farm world, draft or scene.');
console.log('Farm profile isolation preserves both stores when a pointer gesture is active.');
