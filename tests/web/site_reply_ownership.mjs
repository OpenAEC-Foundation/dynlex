import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>false});
assert.equal(result.output,'Cancelled input replies accept their declared result shapes and release only owned compiler, graphics and audio resources.\n');
assert.deepEqual(result.requests.filter(request=>request.service!=='fixture'),[
  {service:'compiler',payload:{operation:'release',artifact:99}},
  {service:'graphics',payload:{operation:'release',resource:41}},
  {service:'graphics',payload:{operation:'release',resource:42}},
  {service:'audio',payload:{operation:'unload',buffers:[71,72]}}
]);
console.log('Production input-loop cancellation regression passed: null, array and object replies; exact artifact, graphics and decoded-audio cleanup.');
