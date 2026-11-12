// Dense surface comparisons use captured data, without the old renderer dependency.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';

const directory=new URL('./models/farm_crops/',import.meta.url);
const manifest=JSON.parse(await fs.readFile(new URL('manifest.json',directory),'utf8'));
const input=process.argv[2];
let actual,read;
if(input.endsWith('.wasm')) {
  const bytes=await fs.readFile(input),chunks=[],filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.json',{data:new TextEncoder().encode(JSON.stringify(manifest.cases)),modificationTime:filesystem.lastTimestamp});
  const runtime=buildRuntimeImports(chunks,filesystem,inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,32*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);
  instance.exports.main();
  actual=JSON.parse(chunks.join(''));
  assert.equal(allocation.live,0,'Crop generation retained allocations');
  read=async name=>{const file=filesystem.files.get(name);assert(file,`Missing crop stream ${name}`);return file.data;};
  console.log('Crop allocations:',allocation.peak,'peak bytes; zero retained');
} else {
  actual=JSON.parse(await fs.readFile(input,'utf8'));
  read=name=>fs.readFile(path.join(path.dirname(input),name));
}
const view=bytes=>new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
const close=(actual,expected,label)=>assert(Math.abs(actual-expected)<0.00001,`${label}: ${actual} != ${expected}`);
const cellSize=0.001;
const key=(x,y,z)=>`${x},${y},${z}`;
function centroid(values,offset) {
  return [0,1,2].map(axis=>Math.floor(((values[offset+axis]+values[offset+9+axis]+values[offset+18+axis])/3)/cellSize));
}
function compareSurface(actual,expected,label) {
  assert.equal(actual.length,expected.length,`${label}: triangle stream size`);
  const count=expected.length/27,buckets=new Map(),used=new Uint8Array(count);
  for(let triangle=0;triangle<count;triangle++) {
    const c=centroid(expected,triangle*27),k=key(...c),bucket=buckets.get(k)??[];
    bucket.push(triangle);buckets.set(k,bucket);
  }
  let maximum=0;
  for(let triangle=0;triangle<count;triangle++) {
    const offset=triangle*27,c=centroid(actual,offset);
    let found=-1,best=Infinity,error=Infinity;
    for(let x=-1;x<=1;x++)for(let y=-1;y<=1;y++)for(let z=-1;z<=1;z++) {
      for(const candidate of buckets.get(key(c[0]+x,c[1]+y,c[2]+z))??[]) {
        if(used[candidate])continue;
        for(let shift=0;shift<3;shift++) {
          let total=0,largest=0;
          for(let corner=0;corner<3;corner++)for(let component=0;component<9;component++) {
            const delta=Math.abs(actual[offset+corner*9+component]-expected[candidate*27+((corner+shift)%3)*9+component]);
            total+=delta*delta;largest=Math.max(largest,delta);
          }
          if(largest<0.00001&&total<best){found=candidate;best=total;error=largest;}
        }
      }
    }
    assert(found!==-1,`${label}: triangle ${triangle} has no matching oriented position/normal/color surface: ${Array.from(actual.slice(offset,offset+27))}`);
    used[found]=1;maximum=Math.max(maximum,error);
  }
  return maximum;
}
assert.equal(actual.length,manifest.cases.length,'Every crop kind and stage must execute');
let triangles=0,maximum=0;
for(let index=0;index<manifest.cases.length;index++) {
  const expected=manifest.cases[index],entry=actual[index];
  assert.equal(entry.id,expected.id);
  assert.equal(entry.indices,expected.vertices,`${entry.id}: triangle count`);
  assert(entry.vertices<entry.indices,`${entry.id}: batching discarded indexed vertex sharing`);
  expected.bounds.forEach((value,axis)=>close(entry.bounds[axis],value,`${entry.id}: bounds ${axis}`));
  for(const field of ['roughness','metalness','opacity'])close(entry[field],expected[field],`${entry.id}: ${field}`);
  for(const field of ['doubleSided','castsShadow','receivesShadow'])assert.equal(entry[field],expected[field],`${entry.id}: ${field}`);
  const p=await read(entry.id+'.vertices'),i=await read(entry.id+'.indices'),c=await read(entry.id+'.colors');
  assert.equal(p.byteLength,entry.vertices*32);assert.equal(i.byteLength,entry.indices*4);assert.equal(c.byteLength,entry.vertices*12);
  const positions=view(p),indices=view(i),colors=view(c),values=new Float32Array(entry.indices*9);
  for(let vertex=0;vertex<entry.indices;vertex++) {
    const source=indices.getUint32(vertex*4,true);
    assert(source<entry.vertices,'A crop index references a missing vertex');
    for(let component=0;component<6;component++)values[vertex*9+component]=positions.getFloat32(source*32+component*4,true);
    for(let component=0;component<3;component++)values[vertex*9+6+component]=colors.getFloat32(source*12+component*4,true);
  }
  const reference=gunzipSync(await fs.readFile(new URL(expected.data,directory)));
  assert.equal(reference.byteLength,expected.bytes);
  const raw=view(reference),floats=new Float32Array(reference.byteLength/4);
  for(let index=0;index<floats.length;index++)floats[index]=raw.getFloat32(index*4,true);
  maximum=Math.max(maximum,compareSurface(values,floats,entry.id));triangles+=expected.triangles;
}
console.log(`PASS Farm crops: ${actual.length} batched beds, ${triangles} oriented triangles; all vertex positions, normals and linear colors match authored surfaces (maximum error ${maximum}).`);
