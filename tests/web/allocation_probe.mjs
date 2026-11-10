import assert from 'node:assert/strict';

// Observe program ownership without counting the runtime's reusable free pool.
export function observeAllocations(runtime,maximumPeak) {
  const allocations=new Map();let live=0,peak=0;
  const remember=(pointer,size)=>{
    assert(pointer,'The fixture ran out of memory');
    assert(!allocations.has(pointer),'The allocator returned an already owned block');
    allocations.set(pointer,size);live+=size;peak=Math.max(peak,live);
    assert(peak<maximumPeak,`Program allocations retained ${peak} bytes`);return pointer;
  };
  const forget=pointer=>{const size=allocations.get(pointer);if(size!==undefined) {live-=size;allocations.delete(pointer);}};
  const {malloc,calloc,realloc,free}=runtime.env;
  runtime.env.malloc=size=>remember(malloc(size),Number(size));
  runtime.env.calloc=(count,size)=>remember(calloc(count,size),Number(count)*Number(size));
  runtime.env.realloc=(pointer,size)=>{const result=realloc(pointer,size);forget(pointer);return remember(result,Number(size));};
  runtime.env.free=pointer=>{forget(pointer);return free(pointer);};
  return {get live(){return live;},get peak(){return peak;}};
}
