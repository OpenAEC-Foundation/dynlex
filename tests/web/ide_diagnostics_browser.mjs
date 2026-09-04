import assert from "node:assert/strict";
import {
  clickElement, closeBrowserSession, command, dispatchKey, evaluate, findMonacoText, navigate,
  replaceMonacoSource, runtimeExceptions, waitFor
} from "./browser_test_driver.mjs";

async function replaceActiveLine(text) {
  await dispatchKey("Home", "Home", 36);
  await dispatchKey("End", "End", 35, 8);
  await command("Input.insertText", { text });
}

async function loadValidSource() {
  await replaceMonacoSource(`import lib/std.dl

print 1 as a line`);
  await waitFor("document.querySelector('#status-text')?.textContent === 'Ready'", "the valid source to compile");
  await findMonacoText("print 1 as a line");
}

const diagnosticExpression = `document.querySelector('#diagnostics-list')?.textContent.includes(
  "the assigned value 'x' has no type"
)`;

await navigate("/ide/");
await waitFor("document.querySelector('#status-text')?.textContent === 'Ready'", "the IDE to initialize");

await loadValidSource();
await replaceActiveLine("add 2 to x");
await waitFor("document.querySelector('#status-text')?.textContent === 'Build failed'", "the active invalid line build");
assert.equal(await evaluate("document.querySelector('#diagnostics-count').textContent"), "0");
await dispatchKey("Escape", "Escape", 27);
await dispatchKey("ArrowUp", "ArrowUp", 38);
await waitFor(diagnosticExpression, "moving off the invalid line to reveal its diagnostic");
await waitFor(
  `document.querySelector('#diagnostics-list .diagnostic-frame')?.textContent.startsWith('while inferring ')`,
  "the inference stack to retain its compiler-provided frame descriptions"
);
await waitFor(
  "document.querySelector('.squiggly-error') !== null",
  "the main-document stack frame to receive an error highlight"
);
await clickElement("#diagnostics-list li");
await waitFor(
  "[...document.querySelectorAll('[data-current-file]')].every((label) => label.textContent === 'std.dl')",
  "the diagnostic to open its owning standard-library file"
);
await waitFor(
  `(() => {
    const defaultColor = getComputedStyle(document.querySelector('.view-lines')).color;
    return new Set(
      [...document.querySelectorAll('.view-lines span')]
        .map((node) => getComputedStyle(node).color)
        .filter((color) => color !== defaultColor)
    ).size >= 3;
  })()`,
  "the erroneous standard-library file to retain semantic highlighting",
  10000
);
await navigate("/ide/");
await waitFor("document.querySelector('#status-text')?.textContent === 'Ready'", "the IDE to reinitialize");
await loadValidSource();
await replaceActiveLine("add 2 to x");
await waitFor("!document.querySelector('#run-button').disabled", "Run to remain available after a failed build");
await clickElement("#run-button");
await waitFor(diagnosticExpression, "Run to commit the active invalid line and reveal its diagnostic");

assert.deepEqual(runtimeExceptions, []);
closeBrowserSession();
console.log("IDE line commits and imported diagnostics are visible in Chrome.");
