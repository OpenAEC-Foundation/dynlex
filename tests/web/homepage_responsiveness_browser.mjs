import assert from "node:assert/strict";
import {
  closeBrowserSession, command, evaluate, onProtocolEvent, runtimeExceptions, siteOrigin, waitFor
} from "./browser_test_driver.mjs";

const heldRequests = new Map();
const unsubscribe = onProtocolEvent("Fetch.requestPaused", event => {
  heldRequests.set(new URL(event.request.url).pathname, event.requestId);
});

try {
  await command("Fetch.enable", { patterns: [
    { urlPattern: "*/editor.js", requestStage: "Request" },
    { urlPattern: "*/shaders/manifest.json", requestStage: "Request" }
  ] });
  await command("Emulation.setDeviceMetricsOverride", { width: 680, height: 900, deviceScaleFactor: 1, mobile: false });
  await command("Page.navigate", { url: `${siteOrigin}/` });
  await waitFor("document.querySelector('.menu-toggle')", "navigation before editor and shader assets finish loading", 5000);
  await evaluate("document.querySelector('.menu-toggle').click()");
  assert.equal(await evaluate("document.querySelector('.menu-toggle').getAttribute('aria-expanded')"), "true");
  await evaluate("document.querySelector('.menu-toggle').click()");
  await evaluate("document.querySelector('#language').scrollIntoView({block:'center',behavior:'instant'})");
  await waitFor("document.querySelector('.language-copy').classList.contains('is-visible')", "page content while previews load", 2000);
  await evaluate("document.querySelector('#tab-use').click()");
  assert.equal(await evaluate("document.querySelector('#panel-use').hidden"), false);
  assert.equal(await evaluate("document.querySelector('.language-execute [data-snippet-run]').disabled"), true,
    "A loading editor must expose its loading state instead of an unresponsive Run button");
  assert.equal(await evaluate("document.querySelector('.language-execute [data-snippet-status]').textContent"), "LOADING");
  console.log("Navigation, content and tabs respond while editor and shader requests are held.");
  await command("Fetch.disable");
  await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length === 5", "editors after releasing asset requests");
  await waitFor("document.querySelector('[data-live-shader-banner]').dataset.shaderPlaylistReady === 'true'", "shader initialization after releasing asset requests");
  await evaluate("document.querySelectorAll('[data-snippet-run]')[0].click()");
  await waitFor("document.querySelector('[data-runnable-sketch]').dataset.runState === 'done'", "the example to run after delayed initialization");
  assert.equal(await evaluate("document.querySelector('[data-snippet-output]').textContent.trim()"), "64");
  assert.deepEqual(runtimeExceptions, []);
  assert.ok(heldRequests.has("/editor.js"));
  assert.ok(heldRequests.has("/shaders/manifest.json"));
} finally {
  unsubscribe();
  await closeBrowserSession();
}
