import assert from 'node:assert/strict';
import {recordWebsiteProgram} from './recording_runtime.mjs';
const memory=new Map(),snapshots=[];let milliseconds=100;
const result=await recordWebsiteProgram(process.argv[2],{
  hooks:{account_fixture(scenario) {
    if(scenario>0) snapshots.push(new Map(memory));
    memory.clear();
  }},
  cancelRequest:()=>false, // A result already queued still owns its reply.
  synchronousService(service,payload) {
    if(service==='clock') return {milliseconds:milliseconds++};
    assert.equal(service,'storage');
    if(payload.action==='keys') return [...memory.keys()];
    if(payload.action==='get') return {value:memory.get(payload.key)??null};
    assert.equal(payload.action,'set');memory.set(payload.key,payload.value);return null;
  }
});
assert.equal(result.output,'Account policy keeps profiles and IDE drafts separate, coalesces writes, preserves in-flight edits, gates stale replies, resolves conflicts and completes finite logout barriers.\n');
assert.equal(JSON.parse(snapshots[0].get('dynlex.games.guest')).games.river.data.source,'guest only');
assert.equal(JSON.parse(snapshots[1].get('dynlex.games.user:alice')).games.river.data.source,'later local edit');
assert.deepEqual(JSON.parse(snapshots[1].get('dynlex.games.guest')),{version:7,games:{},lastGame:null});
assert.equal(JSON.parse(snapshots[2].get('dynlex.games.backup:alice')).games.river.data.source,'local conflicted');
assert.equal(JSON.parse(snapshots[2].get('dynlex.games.user:alice')).games.river.data.source,'keep my game');
assert.equal(memory.get('dynlex.games.profile'),'user:charlie');
assert.equal(JSON.parse(memory.get('dynlex.games.user:charlie')).games.river.data.source,'charlie plan');
const groups=[];
for(const request of result.requests) {
  if(request.service==='firebase'&&request.payload.operation==='connect') groups.push([]);
  if(request.service==='firebase') groups.at(-1).push(request.payload);
}
assert.equal(groups.length,3);
for(const group of groups) {
  assert.equal(group[0].operation,'connect');assert.equal(group[1].operation,'watch');
  for(const request of group.filter(item=>item.operation==='read'||item.operation==='compare-set')) {
    assert.equal(request.field,'payload');assert.equal(request.path[0],'players');
    assert.deepEqual(request.path.slice(2),['games','progress']);
  }
}
const sources=group=>group.filter(item=>item.operation==='compare-set').map(item=>JSON.parse(item.data.payload).games.river.data.source);
assert.deepEqual(sources(groups[0]),['second edit','edit during write','logout snapshot']);
assert.deepEqual(sources(groups[1]),['local conflicted','keep my game','keep my game']);
assert.deepEqual(sources(groups[2]),['unflushed alice']);
const kept=groups[1].filter(item=>item.operation==='compare-set').at(-1);
assert.equal(JSON.parse(kept.expected).games.river.data.source,'remote changed again');
assert.equal(kept.data.version,7);assert.equal(kept.timestampField,'updatedAt');
assert.equal(result.requests.filter(request=>request.service==='log').length,0);
assert.equal(result.requests.filter(request=>request.service==='timer').length,5);
console.log('DynLex account controller tests passed: generations, debounce, in-flight edits, conflict backup, fresh comparisons, profile isolation and finite logout.');
