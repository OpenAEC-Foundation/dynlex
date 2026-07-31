import assert from "node:assert/strict";
import {
  captureScreenshot,
  clickElement,
  closeBrowserSession,
  dispatchKey,
  evaluate,
  navigate,
  requestedUrls,
  runtimeExceptions,
  waitFor
} from "./browser_test_driver.mjs";
import {
  assertRiverChallengeLoadingBoundary,
  runRiverChallengeBrowserTest
} from "./river_challenge_browser.mjs";

await navigate("/");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]').length === 2",
  "the homepage"
);
await waitFor("document.fonts.status === 'loaded'", "the homepage fonts");
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.shaderPlaylistReady === 'true'",
  "homepage initialization"
);
assertRiverChallengeLoadingBoundary(requestedUrls);
await runRiverChallengeBrowserTest({
  captureScreenshot,
  clickElement,
  dispatchKey,
  evaluate,
  requestedUrls,
  waitFor
});
assert.deepEqual(runtimeExceptions, [], "The river challenge must not raise browser exceptions");
await closeBrowserSession();
console.log("Homepage river challenge compiles and runs in Chrome.");
