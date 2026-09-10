import assert from 'node:assert/strict';
import {savedCollectionRole} from './farm_collection_fixture.mjs';
import {createProgressStore,readProgress,SAVE_VERSION} from '../../web/game-progress.js';
import {targetId} from '../../web/farm-vocabulary.js';

const before=savedCollectionRole(),encoded=JSON.stringify(before);
const memory=new Map(['guest','user:alice','backup:alice'].map(id=>['dynlex.games.'+id,encoded]));
const storage={get length(){return memory.size;},key:i=>[...memory.keys()][i],getItem:key=>memory.get(key)??null,setItem:(key,value)=>memory.set(key,value)};
createProgressStore(storage,error=>{throw error;});
for (const text of [...memory.values(),JSON.stringify(readProgress(encoded))]) {
  const save=JSON.parse(text),data=save.games.farm.data,old=before.games.farm.data;
  assert.equal(save.version,SAVE_VERSION);
  assert.equal(data.roles[0].program[1].key,'collect');
  assert.equal(data.roles[0].program[3].key,'collect');
  assert.equal(data.roles[0].program[3].target,targetId('compost'));
  assert.equal(data.roles[0].needsRebuild,true);
  assert.equal(data.roles[0].source,old.roles[0].source);
  assert.equal(data.roles[0].applied,old.roles[0].applied);
  assert.deepEqual(data.roles[1],old.roles[1],'An unfinished draft stays unapplied and needs no rebuild');
  assert.deepEqual(data.world,old.world,'Migrating commands preserves the farm and running worker state');
  assert.deepEqual(readProgress(text),save,'Current saves need no compatibility conversion');
}
console.log('Version 6 collection programs migrate in guest, account, backup and cloud saves without discarding drafts or worker state.');
