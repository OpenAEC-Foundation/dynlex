import assert from "node:assert/strict";
import { editCode, readCode } from "./editor_test_driver.mjs";
import {
  closeBrowserSession, command, evaluate, navigate, runtimeExceptions, siteOrigin, waitFor
} from "./browser_test_driver.mjs";

const homepageResponse = await fetch(`${siteOrigin}/`);
assert.equal(homepageResponse.headers.get('cache-control'), 'no-cache', 'Navigation tests must use deployment cache headers');
assert.equal(homepageResponse.headers.get('clear-site-data'), null);

await command("Page.addScriptToEvaluateOnNewDocument", {
  source: `
    window.navigationTest = { restored: 0, workers: 0, terminated: 0, messages: [] };
    addEventListener("pageshow", event => {
      if (event.persisted) navigationTest.restored += 1;
    });
    const NativeWorker = window.Worker;
    window.Worker = class extends NativeWorker {
      constructor(...args) {
        super(...args);
        this.navigationCompilerWorker = new URL(args[0], location.href).pathname.endsWith('/compiler-worker.js');
        if (this.navigationCompilerWorker) navigationTest.workers += 1;
      }
      postMessage(message, ...args) {
        if (this.navigationCompilerWorker) navigationTest.messages.push(message);
        return super.postMessage(message, ...args);
      }
      terminate() {
        if (this.navigationCompilerWorker) navigationTest.terminated += 1;
        return super.terminate();
      }
    };
  `
});

async function runSketch(index, expectedOutput) {
  await evaluate(`document.querySelectorAll('[data-snippet-run]')[${index}].click()`);
  await waitFor(
    `['done', 'error'].includes(document.querySelectorAll('[data-runnable-sketch]')[${index}].dataset.runState)`,
    `homepage sketch ${index} to finish`,
    30000
  );
  const result = await evaluate(`(() => {
    const sketch = document.querySelectorAll('[data-runnable-sketch]')[${index}];
    return {
      state: sketch.dataset.runState,
      output: sketch.querySelector('[data-snippet-output]').textContent.trim(),
      disabled: sketch.querySelector('[data-snippet-run]').disabled
    };
  })()`);
  assert.deepEqual(result, { state: "done", output: expectedOutput, disabled: false });
}

try {
  await navigate("/");
  await waitFor(
    "document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length === 5",
    "homepage editors connected"
  );
  await runSketch(0, "64");
  await editCode('[data-snippet-source]', (await readCode('[data-snippet-source]')).replace('square 8', 'square 9'));
  await waitFor(
    "document.querySelector('[data-runnable-sketch]').dataset.runState === 'edited'",
    "edited square example"
  );
  await runSketch(0, "81");

  for (let visit = 1; visit <= 2; visit += 1) {
    const history = await command("Page.getNavigationHistory");
    await command("Page.navigate", { url: `${siteOrigin}/wiki/` });
    await waitFor(
      "location.pathname === '/wiki/' && document.readyState === 'complete'",
      "documentation page"
    );
    await command("Page.navigateToHistoryEntry", {
      entryId: history.entries[history.currentIndex].id
    });
    await waitFor(
      `location.pathname === '/' && window.navigationTest?.restored === ${visit}`,
      "homepage restored from the browser's back/forward cache",
      5000
    );
    await runSketch(0, "81");
    await runSketch(1, "The sketch is alive.");
    await runSketch(2, "42");
    assert.deepEqual(
      await evaluate("({ workers: navigationTest.workers, terminated: navigationTest.terminated })"),
      { workers: 1, terminated: 0 },
      "Restored sketches must retain their live compiler worker"
    );
  }

  // The final page disposal must still clean up after earlier cached navigations.
  await evaluate("dispatchEvent(new PageTransitionEvent('pagehide', { persisted: false }))");
  await waitFor("navigationTest.terminated === 1", "compiler worker shutdown on final disposal");
  assert.deepEqual(
    await evaluate(`navigationTest.messages
      .map(message => message.payload?.message?.method)
      .filter(method => ['shutdown', 'exit'].includes(method))`),
    ["shutdown", "exit"]
  );
  assert.deepEqual(runtimeExceptions, []);
  console.log("All homepage sketches run after repeated Back navigation, preserve edits, and shut down on final disposal.");
} finally {
  closeBrowserSession();
}
