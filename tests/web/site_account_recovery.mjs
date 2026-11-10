import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const memory=new Map();let milliseconds=100;
const result=await recordWebsiteProgram(process.argv[2],{
  cancelRequest:()=>false,
  synchronousService(service,payload) {
    if(service==='clock') return {milliseconds:milliseconds++};
    assert.equal(service,'storage');
    if(payload.action==='keys') return [...memory.keys()];
    if(payload.action==='get') return {value:memory.get(payload.key)??null};
    assert.equal(payload.action,'set');memory.set(payload.key,payload.value);return null;
  }
});
assert.equal(result.output,'Account failures preserve committed profiles and corrupt sources; online, retry and observer recovery keep stale operations retired.\n');
assert.equal(memory.get('dynlex.games.profile'),'guest');
assert.equal(JSON.parse(memory.get('dynlex.games.user:alice')).games.river.data.source,'offline draft');
assert.equal(JSON.parse(memory.get('dynlex.games.user:bob')).games.river.data.source,'bob while watcher ends');
const requests=operation=>result.requests.filter(request=>request.service==='firebase'&&request.payload.operation===operation);
assert.equal(requests('connect').length,2);assert.equal(requests('watch').length,2);
assert.equal(requests('compare-set').length,4);assert.equal(requests('sign-out').length,0);
assert.equal(result.requests.filter(request=>request.service==='log').length,5);
assert(result.requests.filter(request=>request.service==='log').every(request=>request.payload.level==='error'));
console.log('DynLex account recovery tests passed: initialization/read/write/auth errors, offline saves, reconnect, corrupt cloud preservation and stale-reply retirement.');
