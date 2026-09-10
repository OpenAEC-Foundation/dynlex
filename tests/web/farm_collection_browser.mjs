import assert from 'node:assert/strict';
import {navigate,waitFor,evaluate,clickElement,command,dispatchKey,runtimeExceptions,consoleErrors,closeBrowserSession} from './browser_test_driver.mjs';
import {editCode,readCode,withEditor} from './editor_test_driver.mjs';
import {savedCollectionRole} from './farm_collection_fixture.mjs';
import {SAVE_VERSION} from '../../web/game-progress.js';
import {NO_TARGET,targetId} from '../../web/farm-vocabulary.js';

const before=savedCollectionRole(),saved="JSON.parse(localStorage.getItem('dynlex.games.guest'))";
const farm=`${saved}.games.farm.data`,worker=`${farm}.world.workers[0]`,editor='[data-farm-editor]';
async function focusSuggestion(label) {
  const matches=`row.textContent.trimStart().startsWith(${JSON.stringify(label)})`;
  await waitFor(`[...document.querySelectorAll('.suggest-widget.visible .monaco-list-row')].some(row=>${matches})`,'completion '+label);
  const offset=await evaluate(`(() => {
    const rows=[...document.querySelectorAll('.suggest-widget.visible .monaco-list-row')];
    return rows.findIndex(row=>${matches})-rows.findIndex(row=>row.classList.contains('focused'));
  })()`);
  for(let index=0;index<Math.abs(offset);index++) await dispatchKey(offset>0?'ArrowDown':'ArrowUp',offset>0?'ArrowDown':'ArrowUp',offset>0?40:38);
  await waitFor(`document.querySelector('.suggest-widget.visible .monaco-list-row.focused')?.textContent.trimStart().startsWith(${JSON.stringify(label)})`,'selected completion '+label);
}
await command('Page.addScriptToEvaluateOnNewDocument',{source:`if (!sessionStorage.getItem('collection-fixture')) {
  localStorage.setItem('dynlex.games.guest',${JSON.stringify(JSON.stringify(before))});
  sessionStorage.setItem('collection-fixture','seeded');
}`});
await navigate('/');
await waitFor("document.querySelector('[data-game-resume]')?.hidden===false",'version 6 save available');
await clickElement('[data-game-resume]');
await waitFor("document.querySelector('[data-farm-editor]')?.dataset.languageReady==='true'",'migrated farm ready');
assert.equal(await evaluate(`${saved}.version`),SAVE_VERSION);
assert.equal(await evaluate(`${farm}.roles[0].needsRebuild`),false);
assert.equal(await evaluate(`${farm}.roles[1].source`),'grab ');
assert.equal(await evaluate(`${farm}.roles[1].program`),null,'An incomplete draft is not compiled during migration');
assert.equal(await evaluate(`${worker}.routine.pc`),1);
assert.deepEqual(await evaluate(`${worker}.routine.loops`),{0:-1});
assert.equal(await readCode(editor),before.games.farm.data.roles[0].source,'Unapplied draft survives migration');
assert.equal(await evaluate(`${farm}.roles[0].program[1].target`),NO_TARGET,'Old implicit barn access rebuilds as nearby collection');
assert.equal(await evaluate(`${farm}.roles[0].program[3].target`),targetId('compost'),'An explicit source stays explicit');
assert.equal(await evaluate(`${farm}.roles[0].program[1].range!==null`),true);
await clickElement('[data-farm-step-worker]');
assert.equal(await evaluate(`${worker}.inventory[0].count`),4,'Saved grab fertilizer now works beside the heap');
assert.equal(await evaluate(`${farm}.world.compost.ready`),0);
await clickElement('[data-farm-step-worker]');
await clickElement('[data-farm-step-worker]');
assert.deepEqual(await evaluate(`${worker}.inventory`),[],'An empty explicit heap leaves nearby ground items alone');
assert.equal(await evaluate(`${farm}.world.tiles[8][4].items[0].count`),4);

const verbs=['grab','take','collect','pick up'];
const source=verbs.flatMap(verb=>[`${verb} 1 fertilizer`,'put down everything',`${verb} 1 fertilizer from the compost heap`,'put down everything']).join('\n');
await editCode(editor,source);
await clickElement('[data-farm-apply]');
await waitFor(`${farm}.roles[0].applied===${JSON.stringify(source)}`,'all aliases compile through browser WASM');
await clickElement('[data-farm-play]');
const plan=await evaluate(`${farm}.roles[0].program`);
assert.equal(plan.length,16);
for (let index=0;index<plan.length;index++) {
  if(index%2===0) {
    assert.equal(plan[index].key,'collect');assert.equal(plan[index].amount,1);
    assert.equal(plan[index].target,index%4===0?NO_TARGET:targetId('compost'));
  }
  await clickElement('[data-farm-step-worker]');
  assert.equal(await evaluate(`${worker}.inventory.reduce((n,item)=>n+item.count,0)`),index%4===0?1:0);
}

await editCode(editor,'grab fertili');
await withEditor(editor,'editor.focus();editor.setPosition({lineNumber:1,column:model.getLineMaxColumn(1)});');
await command('Input.insertText',{text:'z'});
await focusSuggestion('fertilizer');
await dispatchKey('Tab','Tab',9);
assert.equal(await readCode(editor),'grab fertilizer');
await focusSuggestion('from ');
await dispatchKey('Tab','Tab',9);
assert.equal(await readCode(editor),'grab fertilizer from ');
await waitFor("document.querySelector('.suggest-widget.visible')!==null",'source suggestions reopen after Tab');
for (const text of 'the comp') await command('Input.insertText',{text});
await focusSuggestion('compost heap');
await dispatchKey('Tab','Tab',9);
assert.equal(await readCode(editor),'grab fertilizer from the compost heap');
assert.deepEqual(runtimeExceptions,[]);assert.deepEqual(consoleErrors,[]);
console.log('Browser WASM migrates and resumes old collection roles, executes all aliases with optional sources, and chains item/source completions.');
await closeBrowserSession();
