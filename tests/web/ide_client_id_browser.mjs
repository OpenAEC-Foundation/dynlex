import assert from "node:assert/strict";
import { closeBrowserSession, command, navigate, runtimeExceptions, waitFor } from "./browser_test_driver.mjs";

await command("Page.addScriptToEvaluateOnNewDocument", {
  source: `Object.defineProperty(Crypto.prototype, "randomUUID", {
    configurable: true,
    value: undefined
  });`
});
await navigate("/ide/");
await waitFor(
  "document.querySelector('#status-text')?.textContent === 'Ready'",
  "the IDE to initialize without crypto.randomUUID"
);

assert.deepEqual(runtimeExceptions, []);
closeBrowserSession();
console.log("The IDE creates LSP client IDs without crypto.randomUUID.");
