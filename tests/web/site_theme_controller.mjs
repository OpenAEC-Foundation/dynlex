import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';

let scenario=-1;
const saved=new Map(),writes=[],reads=[];
const result=await recordWebsiteProgram(process.argv[2],{
  allocationLimit:32*1024*1024,
  hooks:{theme_fixture(id) {
    scenario=id;
    if(id===1) saved.set(id,'light');
    if(id===2) saved.set(id,'blue');
    if(id===4) saved.set(id,'dark');
  }},
  synchronousService(service,payload) {
    assert.equal(service,'storage');
    assert.equal(payload.key,'dynlex-web-theme');
    if(payload.action==='get') {
      reads.push(scenario);
      if(scenario===3) throw new Error('private read details');
      return {value:saved.get(scenario)??null};
    }
    assert.equal(payload.action,'set');
    if(scenario===4) throw new Error('private write details');
    writes.push({scenario,value:payload.value});
    saved.set(scenario,payload.value);
    return null;
  }
});
assert.equal(result.output,'Studio preserves theme preferences, source colors and drafts, restores the site background, and reports storage failures explicitly.\n');
assert.deepEqual(reads,[0,1,2,3,4]);
assert.deepEqual(writes,[{scenario:0,value:'light'},{scenario:0,value:'dark'}]);
assert.equal(saved.get(2),'blue','Reading a bad preference overwrote its source data');
assert.equal(saved.get(4),'dark','A failed write replaced the stored preference');
assert.equal(result.requests.length,3);
assert(result.requests.every(request=>request.service==='log'));
assert(!JSON.stringify(result.requests).includes('private'));
assert.equal(result.allocations.live,0,'The theme controller retained owned allocations');
console.log('Website theme controller passed: stored values, default dark, live colors, undo and storage failures.');
