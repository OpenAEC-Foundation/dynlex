import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

const frames=[],queries=[];
const {output,requests,shaderCalls,uniformCalls}=await recordWebsiteProgram(process.argv[2],{
  shaderQuery:query=>{queries.push(query);return 73;},
  renderShader:frame=>{frames.push(frame);return 100;}
});
assert.equal(output,'Shader scenes upload raw typed buffers and choose their own draw ranges.\n');
assert.equal(requests.length,0);
assert.deepEqual(queries,[{operation:'records',surface:7,resource:41,pass:1,slot:0}]);
assert.deepEqual(shaderCalls[0],{operation:'draws',surface:7,resource:41,pass:1,data:[73,1,0,0,0,0,0]});
assert.deepEqual(frames.map(frame=>[frame.surface,frame.resource,frame.width,frame.height,new DataView(Uint8Array.from(frame.inputs.find(input=>input.binding===0).data).buffer).getFloat32(0,true)]),[
  [7,42,390,220,0],[7,42,390,220,0.5],[7,42,391,220,0.5]
]);
assert.equal(uniformCalls.length,12,'Each rendered pass must upload its four reflected inputs');
assert.deepEqual(frames.map(frame=>frame.inputs.map(input=>[input.pass,input.group,input.binding,input.scalar,input.rows,input.columns,input.data.length])),Array(3).fill([[0,0,0,0,1,1,16],[0,0,1,0,1,1,16],[0,0,2,0,1,1,16],[0,0,3,0,1,1,16]]));
assert.deepEqual(frames.map(frame=>new DataView(Uint8Array.from(frame.inputs.find(input=>input.binding===2).data).buffer).getFloat32(0,true)),[390,390,391]);
assert.deepEqual(shaderCalls.slice(1).map(call=>call.operation),['vertex','index','draws','vertex','index','draws']);
for(let offset=1;offset<shaderCalls.length;offset+=3){
  const [vertices,indices,draw]=shaderCalls.slice(offset,offset+3);
  assert.equal(vertices.surface,7);assert.equal(vertices.resource,42);assert.equal(vertices.pass,0);assert.equal(vertices.slot,0);
  assert.equal(vertices.data.length%16,0);
  const values=new Float32Array(Uint8Array.from(vertices.data).buffer);
  assert.deepEqual([...values.slice(0,12)],[-1,-1,0,0,3,-1,0,0,-1,3,0,0]);
  assert(indices.data.every(index=>index<vertices.data.length/16));
  assert.deepEqual(draw,{operation:'draws',surface:7,resource:42,pass:0,data:[indices.data.length,1,0,0,0,1,0]});
  if(offset===1){
    assert.equal(vertices.data.length,6374*16);assert.equal(indices.data.length,36534);
    assert.equal(indices.data.reduce((hash,value)=>Math.imul(hash^value,16777619)>>>0,2166136261),848318437);
  }
}
console.log('Website raw shader buffers, loaded-asset ranges and width-only geometry updates passed.');
