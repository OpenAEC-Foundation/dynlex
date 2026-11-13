import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const frames=[];
const {output,requests,shaderCalls,uniformCalls,allocations}=await recordWebsiteProgram(process.argv[2],{
  allocationLimit:32*1024*1024,
  renderShader:frame=>{frames.push(frame);return 100;}
});
assert.equal(output,'Farm rendering retains baked lighting and static geometry, reuses unchanged frames, and refreshes changed world poses.\n');
assert.equal(requests.length,0);
assert.deepEqual(frames.map(frame=>[frame.surface,frame.resource,frame.width,frame.height]),[
  [7,42,480,360],[7,42,480,360],[7,42,600,360],[7,42,600,360]
]);
const geometry=shaderCalls.filter(call=>call.operation==='vertex'&&call.slot<2);
assert.equal(geometry.length,4,'Unchanged immutable meshes must upload once per shader pass');
assert.equal(shaderCalls.filter(call=>call.operation==='index').length,2);
assert.deepEqual([...new Set(geometry.map(call=>call.pass))].sort((a,b)=>a-b),[49,50]);
assert.equal(shaderCalls.filter(call=>call.operation==='vertex'&&call.slot===2&&call.pass===50).length,3,
  'Water-only frames must reuse scene instances, while resizing refreshes camera ordering');
assert.equal(shaderCalls.filter(call=>call.operation==='vertex'&&call.slot===2&&call.pass===49).length,2,
  'Camera changes must retain the sunlight shadow map and its instance buffer');
const enabled=shaderCalls.filter(call=>call.operation==='pass enabled');
for(let pass=0;pass<=48;pass++) {
  const calls=enabled.filter(call=>call.pass===pass);
  assert.equal(calls.length,1,'Baked lighting must be disabled once, without recurring GPU passes');
  assert.equal(calls[0].enabled,false);
}
assert.deepEqual(enabled.filter(call=>call.pass===49).map(call=>call.enabled),[true,false,false,false,true,false]);
const time=frame=>new DataView(Uint8Array.from(frame.inputs.find(input=>input.pass===50&&input.binding===2).data).buffer).getFloat32(12,true);
assert.deepEqual(frames.map(time).map(value=>Math.round(value*10)),[0,1,1,4]);
assert.equal(uniformCalls.filter(call=>call.pass<48).length,96,'Cube bake uniforms must be supplied once');
assert.equal(uniformCalls.filter(call=>call.pass===48).length,1,'DFG bake parameters must be supplied once');
assert.deepEqual(shaderCalls.at(-1),{operation:'close',surface:7,resource:42});
assert.equal(allocations.live,0,'Closing the Farm renderer must release all its owned scene, frame and palette allocations');
console.log('Farm renderer ownership, frame/geometry reuse, bake lifetime and shadow refresh passed.');
