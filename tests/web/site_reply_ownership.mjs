import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>false});
assert.equal(result.output,'Cancelled input replies accept their declared result shapes and release only owned compiler, graphics and audio resources.\n');
assert.deepEqual(result.requests.filter(request=>request.service!=='fixture'),[
  {service:'compiler',payload:{operation:'release',artifact:99}},
  {service:'audio',payload:{operation:'unload',buffers:[71,72]}}
]);
assert.deepEqual(result.shaderCalls.filter(call=>call.operation==='close').map(call=>call.resource),[41,42,43]);
console.log('Production input-loop cancellation regression passed: null, array and object replies; exact artifact, graphics and decoded-audio cleanup.');
