import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {navigate,waitFor,evaluate,clickElement,captureScreenshot,runtimeExceptions,consoleErrors,closeBrowserSession} from "./browser_test_driver.mjs";
import {editCode,withEditor} from "./editor_test_driver.mjs";
import {targetId} from "../../web/farm-vocabulary.js";

const colors=JSON.parse(await readFile(new URL('../../shared/call-colors.json',import.meta.url),'utf8'));
const rgb=hex=>`rgb(${hex.match(/../g).map(part=>parseInt(part,16)).join(', ')})`;
const saved="JSON.parse(localStorage.getItem('dynlex.games.guest'))", farm=`${saved}.games.farm.data`, worker=`${farm}.world.workers[0]`;
await navigate('/');
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5",'shared editors ready');
await clickElement('[data-farm-challenge-load]');
await waitFor("document.querySelector('[data-farm-editor]')?.dataset.languageReady==='true'",'farm editor ready');
await clickElement('[data-farm-pause-worker]');
await editCode('[data-farm-editor]',`walk to the red flag
if it's still there:
    if i can reach it:
        wait
walk to the barn
store 2 logs
store everything
walk to the nearest cow`);
await withEditor('[data-farm-editor]',"editor.layout({width:500,height:350});editor.revealLine(1);");
await evaluate("document.querySelector('[data-farm-editor]').scrollIntoView({block:'center',behavior:'instant'})");
const spansFor=line=>`(() => {const line=[...document.querySelectorAll('[data-farm-editor] .view-line')].find(node=>node.textContent.replaceAll('\\u00a0',' ').trim()===${JSON.stringify(line)});return line?[...line.querySelectorAll('span')].filter(span=>!span.children.length && span.textContent.trim()).map(span=>({text:span.textContent.replaceAll('\\u00a0',' ').trim(),color:getComputedStyle(span).color})):[]})()`;
for(const [line,word,depth] of [["if it's still there:",'it',1],['if i can reach it:','it',1],['store everything','everything',1],['store 2 logs','logs',2]]) {
  await waitFor(`${spansFor(line)}.some(span=>span.text===${JSON.stringify(word)} && span.color===${JSON.stringify(rgb(colors[depth].dark))})`,line+' resolves nested arguments');
}
await captureScreenshot('farm-parameterized-code');
await clickElement('[data-farm-apply]');
await waitFor(`${farm}.roles[0].program !== null`,'new commands compile through browser WASM');
assert.equal(await evaluate(`${farm}.roles[0].program[5].amount`),2);
assert.equal(await evaluate(`${farm}.roles[0].program.at(-1).target`),targetId('cow'));
await clickElement('[data-farm-step-worker]');
const subject=await evaluate(`${worker}.subject`);
assert.equal(subject.kind,'target');assert.equal(subject.value.kind,'flag');assert.equal(subject.value.id,'red');
const continuation=await evaluate(`${worker}.routine.continuation`);
assert.deepEqual(continuation.target,subject.value);

const origin=await evaluate('performance.timeOrigin');
await navigate('/');
await waitFor(`performance.timeOrigin!==${origin} && document.querySelector('[data-game-resume]')?.hidden===false`,'saved subject available');
await clickElement('[data-game-resume]');
await waitFor("document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded==='farm'",'farm resumed');
assert.deepEqual(await evaluate(`${worker}.subject`),subject);
assert.deepEqual(await evaluate(`${worker}.routine.continuation`),continuation);
await clickElement('[data-remove-flag=red]');
await evaluate("document.querySelector('[data-farm-flag-color]').value='red'");
await clickElement('[data-farm-add-flag]');
assert.notEqual(await evaluate(`${farm}.world.flags.find(flag=>flag.id==='red').serial`),subject.value.serial);
await clickElement('[data-farm-step-worker]');
assert.match(await evaluate(`${worker}.status`),/no longer there/);
await clickElement('[data-farm-step-worker]');
assert.equal(await evaluate(`${worker}.routine.trace.branch`),false);
assert.equal(await evaluate(`${worker}.routine.pc`),4,'A replacement flag does not make the old subject exist again');
assert.deepEqual(runtimeExceptions,[]);assert.deepEqual(consoleErrors,[]);
console.log('Browser WASM compiles argument-based commands; nested colors, remembered targets, save/reload and still-there branching agree.');
await closeBrowserSession();
