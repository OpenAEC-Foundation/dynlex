import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const result=await recordWebsiteProgram(process.argv[2],{cancelRequest:()=>true,allocationLimit:32*1024*1024});
assert.equal(result.output,'Saved Farm roles rebuild applied snapshots without changing drafts, worker positions or loop frames; failures retry explicitly and leaving cancels work.\n');
const compiles=result.requests.filter(item=>item.service==='compiler'&&item.payload.operation==='compile'&&item.payload.entry==='farm.dl');
assert.equal(compiles.length,6);
for(const item of compiles) {
  assert.equal(item.payload.entry,'farm.dl');assert.equal(item.payload.traced,true);
  assert(item.payload.sources['farm.dl'].startsWith('import lib/farm_challenge.dl\n\nloop 3 times:\n    turn right'));
  assert(!item.payload.sources['farm.dl'].includes('draft'));
}
assert(compiles[1].payload.sources['farm.dl'].endsWith('# Another saved role'));
assert.equal(result.requests.filter(item=>item.service==='log').length,2);
assert.equal(result.requests.filter(item=>item.service==='program').length,5);
const lastCompilerId=result.requests.findLastIndex(item=>item===compiles.at(-1))+1;
assert(result.cancelled.includes(lastCompilerId),'Leaving must cancel the pending applied-source compilation.');
assert.equal(result.cancelled.at(-1),3,'The unrelated sketch must keep its runner until the test explicitly stops it.');
assert.equal(result.allocations.live,0);
console.log('Farm rebuilds saved roles sequentially, isolates snapshots and drafts, preserves execution, gates playback, retries failures, and cancels with no retained allocations.');
