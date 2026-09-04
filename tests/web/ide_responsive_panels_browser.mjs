import assert from "node:assert/strict";
import {
  clickElement, closeBrowserSession, command, dispatchKey, evaluate, navigate, runtimeExceptions, waitFor
} from "./browser_test_driver.mjs";

await navigate("/ide/");
await waitFor("document.querySelector('#status-text')?.textContent === 'Ready'", "the IDE to initialize");
await command("Emulation.setDeviceMetricsOverride", {
  width: 680,
  height: 760,
  screenWidth: 680,
  screenHeight: 760,
  deviceScaleFactor: 1,
  mobile: false
});
await waitFor(
  "getComputedStyle(document.querySelector('#project-panel-button')).display !== 'none'",
  "the responsive panel controls to appear"
);
await waitFor(
  `(() => {
    const files = document.querySelector('#project-panel').getBoundingClientRect();
    const tools = document.querySelector('#tool-panel').getBoundingClientRect();
    return files.right <= 0 && tools.left >= innerWidth;
  })()`,
  "the closed responsive panels to leave the viewport"
);
const headerControls = await evaluate(`(() => {
  const controls = [...document.querySelectorAll(
    '#project-panel-button, #tool-panel-button, #theme-button, #run-button'
  )];
  return {
    allVisible: controls.every((control) => getComputedStyle(control).display !== 'none'),
    allFit: controls.every((control) => {
      const bounds = control.getBoundingClientRect();
      return bounds.left >= 0 && bounds.right <= innerWidth;
    })
  };
})()`);
assert.deepEqual(headerControls, { allVisible: true, allFit: true });

const panelState = async () => evaluate(`(() => {
  const app = document.querySelector('#app');
  const files = document.querySelector('#project-panel').getBoundingClientRect();
  const tools = document.querySelector('#tool-panel').getBoundingClientRect();
  return {
    filesOpen: app.classList.contains('project-panel-open'),
    toolsOpen: app.classList.contains('tool-panel-open'),
    filesExpanded: document.querySelector('#project-panel-button').getAttribute('aria-expanded'),
    toolsExpanded: document.querySelector('#tool-panel-button').getAttribute('aria-expanded'),
    filesInert: document.querySelector('#project-panel').inert,
    toolsInert: document.querySelector('#tool-panel').inert,
    filesVisible: files.right > 0 && files.left < innerWidth,
    toolsVisible: tools.right > 0 && tools.left < innerWidth
  };
})()`);

assert.deepEqual(await panelState(), {
  filesOpen: false,
  toolsOpen: false,
  filesExpanded: "false",
  toolsExpanded: "false",
  filesInert: true,
  toolsInert: true,
  filesVisible: false,
  toolsVisible: false
});

await clickElement("#project-panel-button");
await waitFor(
  "document.querySelector('#project-panel').getBoundingClientRect().right > 0",
  "the Files drawer to enter the viewport"
);
assert.deepEqual(await panelState(), {
  filesOpen: true,
  toolsOpen: false,
  filesExpanded: "true",
  toolsExpanded: "false",
  filesInert: false,
  toolsInert: true,
  filesVisible: true,
  toolsVisible: false
});

await clickElement("#tool-panel-button");
await waitFor(
  `document.querySelector('#tool-panel').getBoundingClientRect().left < innerWidth
    && document.querySelector('#project-panel').getBoundingClientRect().right <= 0`,
  "the Tools drawer to enter the viewport"
);
assert.deepEqual(await panelState(), {
  filesOpen: false,
  toolsOpen: true,
  filesExpanded: "false",
  toolsExpanded: "true",
  filesInert: true,
  toolsInert: false,
  filesVisible: false,
  toolsVisible: true
});

await dispatchKey("Escape", "Escape", 27);
await waitFor(
  "document.querySelector('#tool-panel').getBoundingClientRect().left >= innerWidth",
  "Escape to close the Tools drawer"
);
assert.equal((await panelState()).toolsOpen, false);

assert.deepEqual(runtimeExceptions, []);
await command("Emulation.clearDeviceMetricsOverride");
closeBrowserSession();
console.log("Responsive IDE panels remain accessible as mutually exclusive drawers.");
