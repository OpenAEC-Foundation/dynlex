import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {recordWebsiteProgram} from './recording_runtime.mjs';
// Captured before retiring the JavaScript implementation; never regenerate on test runs.
const {input,expected}=JSON.parse(await readFile(new URL('./fixtures/progress-controller.json',import.meta.url),'utf8'));
const text=JSON.stringify(input),memory=new Map(),writes=[];
let quota=false,corrupt=false,phase=0,stamps=0;
const {output,requests,calls}=await recordWebsiteProgram(process.argv[2],{
  hooks:{progress_fixture(id) {
    phase=id;
    if(id===1) {quota=true;return;}
    if(id===3) {
      assert.equal(memory.get('dynlex.games.guest'),'{corrupt');corrupt=false;
      memory.clear();memory.set('dynlex.games.guest',JSON.stringify({version:7,games:{river:{updatedAt:1794279584000,data:{source:'old plan',solved:true,coding:{
        passenger:{count:5,seen:['loaded','future-key']},deliveries:{count:4,seen:['SHEEP','other']},rescue:{count:0,seen:[]}
      }}}},lastGame:'river'}));return;
    }
    if(id===0) {
      for(const key of ['guest','user:alice','backup:alice']) memory.set('dynlex.games.'+key,text);
      memory.set('dynlex.games.profile','user:alice');memory.set('unrelated','untouched');
    } else {
      for(const key of ['guest','user:alice','backup:alice']) assert.equal(JSON.parse(memory.get('dynlex.games.'+key)).version,7);
      assert.deepEqual(JSON.parse(memory.get('dynlex.games.guest')),expected);
      assert.deepEqual(JSON.parse(memory.get('dynlex.games.backup:alice')),expected);
      const saved=JSON.parse(memory.get('dynlex.games.user:alice'));
      assert.equal(saved.games.river.data.source,'cross the river');
      assert.deepEqual(saved.games.river.data.coding.passenger,{count:1,seen:['loaded']});
      assert.equal(saved.games.river.updatedAt,1794279584123);
      assert.deepEqual(saved.games.farm,expected.games.farm,'Saving River changed the farm');
      assert.equal(stamps,2,'An unchanged save called the clock');
      corrupt=true;quota=false;memory.clear();memory.set('dynlex.games.guest','{corrupt');
    }
  }},
  synchronousService(service,payload) {
    if(service==='clock') {stamps++;return {milliseconds:1794279584123};}
    assert.equal(service,'storage');
    if(payload.action==='keys') return [...memory.keys()];
    if(payload.action==='get') return {value:memory.get(payload.key)??null};
    assert.equal(payload.action,'set');
    assert.equal(corrupt,false,'The damaged source was overwritten');
    if(quota) throw new Error('private quota details');
    writes.push({phase,key:payload.key});memory.set(payload.key,payload.value);return null;
  }
});
assert.equal(output,'All stored profiles upgrade, River resumes, goals stay distinct, and failed or corrupt saves remain intact.\n');
const current=JSON.parse(memory.get('dynlex.games.guest'));
assert.deepEqual(current.games.river.data.coding,{passenger:{count:5,seen:['loaded','future-key']},
  deliveries:{count:5,seen:['SHEEP','other','WOLF']},rescue:{count:1,seen:['solved']}});
assert.equal(writes.length,5,'Each old profile upgrades once and only changed data is persisted');
assert.equal(requests.length,2);assert(requests.every(request=>request.service==='log'));
assert(!JSON.stringify(requests).includes('private quota details'));
assert(calls.length>10);
console.log('Website progress controller tests passed.');
