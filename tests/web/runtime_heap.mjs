import assert from 'node:assert/strict';
import {createFreeRangeIndex} from '../../src/web/ide/public/compiler/runtimeHeap.js';
const heap=createFreeRangeIndex();
heap.release(64,32);heap.release(16,32);
assert.equal(heap.take(16),16);assert.equal(heap.take(32),64);
heap.release(16,16);heap.release(48,16);heap.release(64,32);
assert.equal(heap.take(80),16,'Both adjacent ranges must coalesce');
assert.equal(heap.take(16),0);
assert.throws(()=>{heap.release(16,32);heap.release(32,16);},/overlap/);

// Independent interval model checks address-ordered first fit under fragmentation.
const actual=createFreeRangeIndex(),free=[{pointer:16,size:160000}],live=[];
actual.release(16,160000);
let state=0x589c12d3;
const random=()=>{state^=state<<13;state^=state>>>17;state^=state<<5;return state>>>0;};
function release(pointer,size) {
  free.push({pointer,size});free.sort((a,b)=>a.pointer-b.pointer);
  for(let i=free.length-1;i>0;i--) if(free[i-1].pointer+free[i-1].size===free[i].pointer) {
    free[i-1].size+=free[i].size;free.splice(i,1);
  }
}
for(let step=0;step<15000;step++) {
  if(live.length!==0&&random()%3===0) {
    const index=random()%live.length,[block]=live.splice(index,1);
    release(block.pointer,block.size);actual.release(block.pointer,block.size);
  } else {
    const size=(random()%32+1)*16,index=free.findIndex(block=>block.size>=size);
    let expected=0;
    if(index!==-1) {
      expected=free[index].pointer;free[index].pointer+=size;free[index].size-=size;
      if(free[index].size===0) free.splice(index,1);
      live.push({pointer:expected,size});
    }
    assert.equal(actual.take(size),expected,`First fit diverged at operation ${step}`);
  }
}
for(const block of live) actual.release(block.pointer,block.size);
assert.equal(actual.take(160000),16,'Fragmented allocations did not recover their complete range');
assert.equal(actual.take(16),0);
console.log('Free-range index: first fit, splitting, two-sided coalescing and 15,000 randomized operations passed.');

// Exercise the public allocator too: metadata must reject stale and interior
// pointers, retain requested byte counts, and survive metadata-page boundaries.
const {buildRuntimeImports,createRuntimeFilesystem}=await import('../../src/web/ide/public/compiler/runtimeImports.js');
const {env}=buildRuntimeImports([],createRuntimeFilesystem(),{heapBase:65536});
const zero=env.malloc(0);
assert.notEqual(zero,0);
env.free(zero);
assert.throws(()=>env.free(zero),/unknown allocation/);
const large=env.malloc(32768),blocker=env.malloc(16);
new Uint8Array(env.__linear_memory.buffer,large,32768).fill(73);
assert.throws(()=>env.free(large+16),/unknown allocation/);
assert.throws(()=>env.realloc(large+1,64),/unknown allocation/);
assert.equal(env.realloc(large,17),large);
const tail=env.malloc(32736);
assert.equal(tail,large+32,'Shrinking must return the aligned suffix to first fit');
const moved=env.realloc(large,65);
assert.notEqual(moved,large);
assert.deepEqual([...new Uint8Array(env.__linear_memory.buffer,moved,17)],new Array(17).fill(73));
assert.throws(()=>env.realloc(large,10),/unknown allocation/);
env.free(tail);env.free(moved);env.free(blocker);
assert.equal(env.calloc(2049,16),65536,'Allocation metadata must cover more than one page');
assert(new Uint8Array(env.__linear_memory.buffer,65536,2049*16).every(byte=>byte===0));
env.free(65536);
console.log('Runtime allocator: zero-size, stale/interior pointers, shrinking, moving and paged metadata passed.');
