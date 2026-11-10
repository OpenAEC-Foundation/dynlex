import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const progress=source=>({version:7,games:{river:{updatedAt:10,data:{source,solved:false,coding:{
  passenger:{count:0,seen:[]},deliveries:{count:0,seen:[]},rescue:{count:0,seen:[]}
}}}},lastGame:'river'});
const guest=JSON.stringify(progress('guest plan')),bob=JSON.stringify(progress('bob plan'));
const memory=new Map(),writes=[];let failMetadata=false,phase=0;
const result=await recordWebsiteProgram(process.argv[2],{
  hooks:{profile_fixture(scenario) {
    phase=scenario;
    if(scenario===1) {failMetadata=true;return;}
    if(scenario===3) {memory.set('dynlex.games.user:broken','{broken');return;}
    if(scenario===2) {
      assert.equal(memory.get('dynlex.games.profile'),'guest');
      assert.equal(memory.get('dynlex.games.guest'),guest);
      assert.equal(memory.get('dynlex.games.user:bob'),bob);
      assert.deepEqual(JSON.parse(memory.get('dynlex.games.user:charlie')),{version:7,games:{},lastGame:null});
    }
    failMetadata=false;memory.clear();memory.set('dynlex.games.guest',guest);memory.set('dynlex.games.user:bob',bob);
  }},
  synchronousService(service,payload) {
    assert.equal(service,'storage');
    if(payload.action==='keys') return [...memory.keys()];
    if(payload.action==='get') return {value:memory.get(payload.key)??null};
    assert.equal(payload.action,'set');writes.push({phase,key:payload.key});
    if(failMetadata&&payload.key==='dynlex.games.profile') throw new Error('metadata quota');
    memory.set(payload.key,payload.value);return null;
  }
});
assert.equal(result.output,'Profile switching commits data before metadata, keeps guests separate, and preserves active or corrupt saves on failure.\n');
assert.deepEqual(writes.map(write=>write.key),['dynlex.games.user:bob','dynlex.games.profile',
  'dynlex.games.guest','dynlex.games.profile','dynlex.games.user:charlie','dynlex.games.profile']);
assert.equal(memory.get('dynlex.games.user:broken'),'{broken');
assert.equal(result.requests.length,2);assert(result.requests.every(request=>request.service==='log'));
console.log('Account profile controller preserves write ordering, isolation, blocked writes and damaged sources.');
