import assert from "node:assert/strict";
import {
  closeBrowserSession,
  evaluate,
  navigate,
  requestedUrls,
  runtimeExceptions,
  waitFor
} from "./browser_test_driver.mjs";

await navigate("/?autorun=true");
await waitFor(
  "document.querySelector('#status-text')?.textContent === 'Finished'",
  "the Vite-served IDE worker to compile and run"
);
assert.equal(await evaluate("document.querySelector('#runtime-output').textContent.trim()"), "64");
assert.deepEqual(
  await evaluate(`Promise.all([
    import('/wgsl-translator.js').then((module) => typeof module.createWgslTranslator),
    fetch('/compiler/dynlex_wgsl_translator.wasm').then((response) => response.status)
  ])`),
  ["function", 200],
  "The Vite server must expose the shared translator module and its WebAssembly binary"
);
for (const resource of [
  "/compiler/compiler-worker.js",
  "/wgsl-translator.js",
  "/compiler/dynlex_wgsl_translator.wasm"
]) {
  assert.ok(
    requestedUrls.some((url) => new URL(url).pathname === resource),
    `The Vite-served IDE must load ${resource}`
  );
}
assert.deepEqual(runtimeExceptions, [], "The Vite-served IDE must not raise browser exceptions");

closeBrowserSession();
console.log("The Vite development server initializes the IDE compiler worker in Chrome.");
