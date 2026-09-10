import assert from "node:assert/strict";
import {navigate,waitFor,evaluate,clickElement,command,captureScreenshot,runtimeExceptions,consoleErrors,closeBrowserSession} from "./browser_test_driver.mjs";
import {editCode,installEditorAccess} from "./editor_test_driver.mjs";

import {SAVE_VERSION} from "../../web/game-progress.js";

const saved = "JSON.parse(localStorage.getItem('dynlex.games.guest'))";
const farm = `${saved}.games.farm.data`;
await navigate("/");
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5","site ready");
await installEditorAccess();
await clickElement("[data-farm-challenge-load]");
await waitFor("document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded==='farm'","farm ready");
assert.equal(await evaluate("document.querySelectorAll('[data-coding-goal]').length"),3);
await clickElement("[data-farm-areas] summary");
await evaluate("document.querySelector('[data-area-name]').value='North garden'");
await clickElement("[data-area-draw]");
async function draw(from,to) {
  await evaluate("document.querySelector('[data-farm-board]').scrollIntoView({block:'center',behavior:'instant'})");
  const rect=await evaluate("document.querySelector('[data-farm-board] canvas').getBoundingClientRect().toJSON()");
  const point=p=>({x:rect.x+rect.width*p[0],y:rect.y+rect.height*p[1]});
  await command("Input.dispatchMouseEvent",{type:"mousePressed",button:"left",clickCount:1,...point(from)});
  await command("Input.dispatchMouseEvent",{type:"mouseMoved",buttons:1,...point(to)});
  await command("Input.dispatchMouseEvent",{type:"mouseReleased",button:"left",clickCount:1,...point(to)});
}
await draw([.36,.53],[.5,.61]);
await waitFor(`${farm}.world.areas.length===1`,"area rectangle saved");
let area=await evaluate(`${farm}.world.areas[0]`);
assert.equal(area.name,"North garden"); assert.ok(area.right>=area.left && area.bottom>area.top);
await captureScreenshot("farm-work-area");
await clickElement("[data-area-list] [data-action=redraw]");
await draw([.4,.51],[.54,.59]);
await waitFor("document.querySelector('[data-farm-board]').dataset.drawingArea==='false'","area redrawn");
const redrawn=await evaluate(`${farm}.world.areas[0]`);
assert.equal(redrawn.id,area.id); assert.notDeepEqual(redrawn,area); area=redrawn;

await clickElement("[data-farm-pause-worker]");
await editCode("[data-farm-editor]",`if there are at least 2 logs in the barn:
    walk to the nearest tree in the area "North garden"
else:
    wait`);
await clickElement("[data-farm-apply]");
await waitFor(`${farm}.roles[0].program !== null`,"area and stock commands compile in WASM");
assert.equal(await evaluate(`${farm}.roles[0].program[1].area`),"North garden");
await clickElement("[data-farm-step-worker]");
assert.match(await evaluate("document.querySelector('[data-farm-execution]').textContent"),/false/);
await clickElement("[data-coding-goal=watering] summary");
assert.match(await evaluate("document.querySelector('[data-coding-goal=watering] details').textContent"),/bucket/);
await captureScreenshot("farm-coding-tools");

let oldOrigin=await evaluate("performance.timeOrigin");
await navigate("/");
await waitFor(`performance.timeOrigin!==${oldOrigin} && document.querySelector('[data-game-resume]')?.hidden===false`,"saved farm available");
await clickElement("[data-game-resume]");
await waitFor("document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded==='farm'","farm restored");
assert.deepEqual(await evaluate(`${farm}.world.areas[0]`),area);
assert.equal(await evaluate(`${farm}.world.workers[0].paused`),true);
await clickElement("[data-farm-areas] summary");
await clickElement("[data-area-list] [data-action=delete]");
assert.equal(await evaluate(`${farm}.world.areas.length`),0);

// Upgrade a real pre-trace save: preserve the live instruction pointer and reconstruct source maps.
await editCode("[data-farm-editor]","to rest:\n    wait\nloop forever:\n    rest");
await clickElement("[data-farm-apply]");
await waitFor(`${farm}.roles[0].applied.startsWith('to rest:')`,"old-format fixture prepared");
await clickElement("[data-farm-step-worker]");
await evaluate(`(() => {
  const save=${saved}; save.version=3; const {world,roles}=save.games.farm.data;
  delete world.areas; delete world.coding;
  for (const actor of [world.player,...world.workers]) delete actor.routine.trace;
  for (const role of roles) {
    delete role.needsRebuild;
    if(role.program) for(const instruction of role.program) {delete instruction.target;delete instruction.range;delete instruction.area;delete instruction.amount;}
  }
  // pagehide saves current state, so apply the old snapshot as the next document starts.
  sessionStorage.setItem('old-save-fixture',JSON.stringify(save));
})()`);
await command("Page.addScriptToEvaluateOnNewDocument",{source:"const old=sessionStorage.getItem('old-save-fixture');if(old){localStorage.setItem('dynlex.games.guest',old);sessionStorage.removeItem('old-save-fixture');}"});
oldOrigin=await evaluate("performance.timeOrigin"); await navigate("/");
await waitFor(`performance.timeOrigin!==${oldOrigin} && document.querySelector('[data-game-resume]')?.hidden===false`,"old save migrated");
await clickElement("[data-game-resume]");
await waitFor("document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded==='farm'","source maps rebuilt");
assert.equal(await evaluate(`${saved}.version`),SAVE_VERSION);
assert.equal(await evaluate(`${farm}.world.workers[0].routine.pc`),1);
assert.equal(await evaluate(`${farm}.roles[0].needsRebuild`),false);
await clickElement("[data-farm-step-worker]");
assert.equal(await evaluate("editorFeedback('[data-farm-editor]','active').text"),"wait");
assert.deepEqual(runtimeExceptions,[]); assert.deepEqual(consoleErrors,[]);
console.log("Real area drawing/redrawing/deletion, stock code, hints, reloads and old-program source-map migration passed.");
await closeBrowserSession();
