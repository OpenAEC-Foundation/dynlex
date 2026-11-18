// Fragmented lifetimes match retained UI data interleaved with temporary JSON.
import {performance} from 'node:perf_hooks';
import {buildRuntimeImports,createRuntimeFilesystem} from '../../src/web/ide/public/compiler/runtimeImports.js';
for(const count of [1000,2000,4000,8000]) {
  const samples=[];
  for(let round=0;round<4;round++) {
    const env=buildRuntimeImports([],createRuntimeFilesystem(),{heapBase:65536}).env;
    const pointers=Array.from({length:count*2},()=>env.malloc(16));
    const start=performance.now();
    for(let index=1;index<pointers.length;index+=2) env.free(pointers[index]);
    for(let index=0;index<count;index++) {const pointer=env.malloc(32);env.free(pointer);}
    const elapsed=performance.now()-start;
    for(let index=0;index<pointers.length;index+=2) env.free(pointers[index]);
    if(round>0) samples.push(elapsed);
  }
  samples.sort((a,b)=>a-b);
  console.log(JSON.stringify({holes:count,milliseconds:samples[1]}));
}
