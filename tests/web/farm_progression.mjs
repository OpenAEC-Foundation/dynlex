import { installEditorAccess } from "./editor_test_driver.mjs";
import assert from "node:assert/strict";
import { clickElement, closeBrowserSession, evaluate, navigate, runtimeExceptions, waitFor } from "./browser_test_driver.mjs";

await navigate("/");
await installEditorAccess();
await waitFor("document.querySelector('[data-live-shader-banner]').dataset.shaderPlaylistReady === 'true'", "homepage initialization");
await clickElement("[data-river-challenge-load]");
await waitFor("document.querySelector('[data-river-challenge]').dataset.challengeState === 'ready'", "river challenge");
const riverSolution = "load sheep\ncross the river\nunload sheep\nrow back\nload wolf\ncross the river\nunload wolf\nload sheep\nrow back\nunload sheep\nload hay\ncross the river\nunload hay\nrow back\nload sheep\ncross the river\nunload sheep";
await evaluate(`(() => {
  const source = editorFor('[data-river-editor-shell]');
  source.executeEdits('test',[{range:source.getModel().getFullModelRange(),text:${JSON.stringify(riverSolution)}}]);

})()`);
await clickElement("[data-river-run]");
await waitFor("document.querySelector('[data-river-challenge]').dataset.challengeState === 'farm'", "river completion loading the farm", 120000);
await waitFor("document.querySelector('[data-farm-board]').dataset.rendered === 'true'", "automatic farm rendering");
assert.equal(await evaluate("document.querySelector('[data-river-game]') === null"), true);
assert.equal(await evaluate("JSON.parse(localStorage.getItem('dynlex.games.guest')).games.river.data.solved"), true, "River completion is saved before opening the farm");
const previousPage = await evaluate("performance.timeOrigin");
await navigate("/");
await waitFor(`performance.timeOrigin !== ${previousPage} && document.querySelector('[data-live-shader-banner]')?.dataset.shaderPlaylistReady === 'true'`, "new homepage initialization");
await clickElement("[data-river-challenge-load]");
await clickElement("[data-farm-challenge-load]");
await waitFor("document.querySelector('[data-river-challenge]').dataset.challengeState === 'farm'", "skipping during river initialization");
await evaluate("new Promise(resolve => setTimeout(resolve, 1000))");
assert.equal(await evaluate("document.querySelector('[data-river-game]') === null"), true);
assert.deepEqual(runtimeExceptions, []);
console.log("The completed river puzzle and its direct skip both load the farm without a late river takeover.");
await closeBrowserSession();
