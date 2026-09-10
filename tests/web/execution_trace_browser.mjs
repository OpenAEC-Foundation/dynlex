import assert from "node:assert/strict";
import { navigate, waitFor, evaluate, clickElement, captureScreenshot, runtimeExceptions, consoleErrors, closeBrowserSession } from "./browser_test_driver.mjs";
import { editCode, installEditorAccess } from "./editor_test_driver.mjs";
await navigate("/");
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5", "site ready");
await installEditorAccess();
await clickElement("[data-farm-challenge-load]");
await waitFor("document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded==='farm'", "farm ready");
await clickElement("[data-farm-pause-worker]");
await editCode("[data-farm-editor]", `to rest:
    wait
if my hands are full:
    turn right
else:
    rest`);
await clickElement("[data-farm-apply]");
await waitFor("document.querySelector('[data-farm-game]').dataset.playing==='true'", "role ready");
await clickElement("[data-farm-step-worker]");
assert.match(await evaluate("document.querySelector('[data-farm-execution]').textContent"), /false/);
assert.equal(await evaluate("editorFeedback('[data-farm-editor]','active').line"), "3");
await clickElement("[data-farm-step-worker]");
assert.equal(await evaluate("editorFeedback('[data-farm-editor]','active').text"), "wait");
await captureScreenshot("farm-step-helper");
await editCode("[data-farm-editor]", "turn left");
assert.equal(await evaluate("editorFeedback('[data-farm-editor]','active')"), null);

await navigate("/");
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5", "site ready for river");
await clickElement("[data-river-challenge-load]");
await waitFor("document.querySelector('[data-river-editor-shell][data-language-ready=true]')", "river ready");
await editCode("[data-river-editor-shell]", `to load a passenger:
    get the sheep in the boat
if 1 = 2:
    get the hay in the boat
else:
    load a passenger
row to the other side`);
await clickElement("[data-river-step]");
await waitFor("document.querySelector('[data-river-game]').dataset.playbackState==='paused'", "river pauses after condition");
assert.match(await evaluate("document.querySelector('[data-river-execution]').textContent"), /false/);
assert.equal(await evaluate("editorFeedback('[data-river-editor-shell]','active').line"), "3");
await clickElement("[data-river-step]");
await waitFor("document.querySelector('[data-river-game]').dataset.playbackState==='paused'", "river pauses after load");
assert.equal(await evaluate("editorFeedback('[data-river-editor-shell]','active').text"), "get the sheep in the boat");
await captureScreenshot("river-step-helper");
await clickElement("[data-river-step]");
await waitFor("document.querySelector('[data-river-status]').textContent==='PLAN ENDED'", "river completes last step");
assert.equal(await evaluate("editorFeedback('[data-river-editor-shell]','active').text"), "row to the other side");
assert.deepEqual(runtimeExceptions, []); assert.deepEqual(consoleErrors, []);
console.log("Both games step through false branches and highlight executed helper instructions using compiler locations.");
await closeBrowserSession();
