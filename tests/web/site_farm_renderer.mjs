import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const frames=[];
const {output,requests,shaderCalls,uniformCalls,allocations}=await recordWebsiteProgram(process.argv[2],{
  allocationLimit:32*1024*1024,
  renderShader:frame=>{frames.push(frame);return 100;}
});
assert.equal(output,'Farm rendering retains baked lighting and static geometry, reuses unchanged frames, and refreshes world poses, navigation markers and rain.\n');
assert.equal(requests.length,0);
assert.deepEqual(frames.map(frame=>[frame.surface,frame.resource,frame.width,frame.height]),[
  [7,42,480,360],[7,42,480,360],[7,42,600,360],[7,42,600,360],
  [7,42,600,360],[7,42,600,360],[7,42,600,360]
]);
const geometry=shaderCalls.filter(call=>call.operation==='vertex'&&call.slot<2);
assert.equal(geometry.length,8,'Static geometry and the newly visible cloud must upload once per shader pass');
assert.equal(shaderCalls.filter(call=>call.operation==='index').length,4);
assert.deepEqual([...new Set(geometry.map(call=>call.pass))].sort((a,b)=>a-b),[49,50]);
assert.equal(shaderCalls.filter(call=>call.operation==='vertex'&&call.slot===2&&call.pass===50).length,5,
  'Animation must reuse scene instances, while resizing and cloud visibility refresh the scene');
assert.equal(shaderCalls.filter(call=>call.operation==='vertex'&&call.slot===2&&call.pass===49).length,2,
  'Camera changes must retain the sunlight shadow map and its instance buffer');
const linear=byte=>((byte/255+0.055)/1.055)**2.4;
const markerInstances=call=>{
  const data=new DataView(Uint8Array.from(call.data).buffer),records=[];
  assert.equal(data.byteLength%176,0,'The instance buffer contains incomplete records');
  for(let offset=0;offset<data.byteLength;offset+=176) {
    const color=[112,116,120].map(channel=>data.getFloat32(offset+channel,true));
    if(color.every((value,index)=>Math.abs(value-[1,linear(228),linear(160)][index])<0.00001)) {
      assert.equal(data.getUint32(offset+164,true),1,'A selection ring must use unlit shading');
      assert.equal(data.getUint32(offset+168,true),0,'A selection ring must not cast a shadow');
      records.push([48,52,56].map(channel=>data.getFloat32(offset+channel,true)));
    }
  }
  return records;
};
const instances=shaderCalls.filter(call=>call.operation==='vertex'&&call.slot===2);
const rings=instances.filter(call=>call.pass===50).map(markerInstances);
assert(rings.every(records=>records.length===1),'Each color upload must include the selected person ring');
assert.notEqual(rings[0][0][0],rings.at(-1)[0][0],'The selection ring did not follow the displayed worker');
assert(instances.filter(call=>call.pass===49).every(call=>markerInstances(call).length===0),
  'The unlit marker leaked into the shadow submission');
const rainInstances=call=>{
  const data=new DataView(Uint8Array.from(call.data).buffer);let count=0;
  for(let offset=0;offset<data.byteLength;offset+=176) {
    if(data.getUint32(offset+160,true)===3) count++;
  }
  return count;
};
assert.deepEqual(instances.filter(call=>call.pass===50).map(rainInstances),[0,0,0,1,0],
  'Rain must submit one cloud only while it is visible');
assert(instances.filter(call=>call.pass===49).every(call=>rainInstances(call)===0),
  'Rain leaked into the sunlight shadow submission');
const enabled=shaderCalls.filter(call=>call.operation==='pass enabled');
for(let pass=0;pass<=48;pass++) {
  const calls=enabled.filter(call=>call.pass===pass);
  assert.equal(calls.length,1,'Baked lighting must be disabled once, without recurring GPU passes');
  assert.equal(calls[0].enabled,false);
}
assert.deepEqual(enabled.filter(call=>call.pass===49).map(call=>call.enabled),[true,false,false,false,true,false,false,false,false]);
const time=frame=>new DataView(Uint8Array.from(frame.inputs.find(input=>input.pass===50&&input.binding===2).data).buffer).getFloat32(12,true);
assert.deepEqual(frames.map(time).map(value=>Math.round(value*10)),[0,1,1,4,4,5,5]);
const rain=frame=>new DataView(Uint8Array.from(frame.inputs.find(input=>input.pass===50&&input.binding===16).data).buffer).getFloat32(0,true);
assert.deepEqual(frames.map(rain),[0,0,0,0,1,1,0],
  'The shader did not receive the current rain strength independently of geometry');
assert.equal(uniformCalls.filter(call=>call.pass<48).length,96,'Cube bake uniforms must be supplied once');
assert.equal(uniformCalls.filter(call=>call.pass===48).length,1,'DFG bake parameters must be supplied once');
assert.deepEqual(shaderCalls.at(-1),{operation:'close',surface:7,resource:42});
assert.equal(allocations.live,0,'Closing the Farm renderer must release all its owned scene, frame and palette allocations');
console.log('Farm renderer ownership, frame/geometry reuse, bake lifetime and shadow refresh passed.');
