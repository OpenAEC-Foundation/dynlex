import assert from "node:assert/strict";
import {
  captureScreenshot, closeBrowserSession, command, evaluate, navigate, runtimeExceptions, waitFor
} from "./browser_test_driver.mjs";

function contrast(foreground, background) {
  const luminance = color => {
    const channels = color.match(/[\d.]+/g).slice(0, 3).map(Number).map(value => {
      const channel = value / 255;
      return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
    });
    return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
  };
  const values = [luminance(foreground), luminance(background)].sort((a, b) => b - a);
  return (values[0] + 0.05) / (values[1] + 0.05);
}

async function checkText(selector, backgroundSelector = selector) {
  const colors = await evaluate(`({
    foreground: getComputedStyle(document.querySelector(${JSON.stringify(selector)})).color,
    background: getComputedStyle(document.querySelector(${JSON.stringify(backgroundSelector)})).backgroundColor
  })`);
  assert.ok(contrast(colors.foreground, colors.background) >= 4.5,
    `${selector} must have readable contrast: ${JSON.stringify(colors)}`);
}

async function viewport(width) {
  await command("Emulation.setDeviceMetricsOverride", {
    width, height: 1000, deviceScaleFactor: 1, mobile: false
  });
}

async function checkLayout() {
  assert.ok(await evaluate("document.documentElement.scrollWidth <= innerWidth"), "No horizontal page overflow");
  await evaluate("document.fonts.ready");
  assert.ok(await evaluate(`['Space Grotesk', 'Inter', 'JetBrains Mono'].every(family =>
    [...document.fonts].some(face => face.family === family && face.status === 'loaded'))`),
  "The brand fonts used by the page must load");
}

try {
  // Exercise the release error UI without navigating away to an actual download.
  await command("Network.enable");
  await command("Network.setBlockedURLs", { urls: ["*api.github.com*"] });
  for (const [page, name] of [["/", "homepage"], ["/wiki/", "docs"], ["/download.html", "download"]]) {
    await viewport(1440);
    await navigate(page);
    await waitFor("document.querySelector('.nav-launch') && document.fonts.status === 'loaded'", `${name} navigation and fonts`);
    await checkText(".nav-launch");
    const hoverPoint = await evaluate(`(() => {
      const box = document.querySelector('.nav-launch').getBoundingClientRect();
      return {x: box.left + box.width / 2, y: box.top + box.height / 2};
    })()`);
    await command("Input.dispatchMouseEvent", { type: "mouseMoved", ...hoverPoint });
    await evaluate("new Promise(resolve => setTimeout(resolve, 200))");
    await checkText(".nav-launch");
    await command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 0, y: 0 });
    await checkLayout();
    await captureScreenshot(`brand-${name}-desktop`);
    if (name === "download") {
      await checkText("#primary-download");
      await checkText("#status-line", ".download-main");
      await evaluate("document.querySelector('.chooser').open = true");
    }
    await viewport(390);
    await checkLayout();
    await evaluate("document.querySelector('.menu-toggle').click()");
    await checkText(".nav-launch");
    assert.ok(await evaluate(`(() => {
      const box = document.querySelector('.nav-launch').getBoundingClientRect();
      return box.left >= 0 && box.right <= innerWidth;
    })()`), "Mobile navigation must fit");
    await evaluate("document.querySelector('.menu-toggle').click()");
    await captureScreenshot(`brand-${name}-mobile`);
  }
  await viewport(1440);
  await navigate("/ide/");
  await waitFor("document.querySelector('#status-text')?.textContent === 'Ready'", "IDE initialization");
  assert.equal(await evaluate("document.documentElement.dataset.theme"), "dark");
  for (const theme of ["dark", "light"]) {
    await checkText("#run-button");
    await checkText(".header-actions .icon-action", ".app-header");
    await checkText(".workspace-context span", ".app-header");
    const brandPoint = await evaluate(`(() => {
      const box = document.querySelector('.ide-brand').getBoundingClientRect();
      return {x: box.left + box.width / 2, y: box.top + box.height / 2};
    })()`);
    await command("Input.dispatchMouseEvent", { type: "mouseMoved", ...brandPoint });
    await evaluate("new Promise(resolve => setTimeout(resolve, 200))");
    await checkText(".ide-brand-name", ".app-header");
    await command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 0, y: 0 });
    await checkText(".monaco-editor .view-lines", ".monaco-editor-background");
    await checkLayout();
    await captureScreenshot(`brand-ide-${theme}`);
    await viewport(390);
    await evaluate("document.querySelector('#project-panel-button').click()");
    await checkText("#project-panel-button", ".app-header");
    await evaluate("document.querySelector('#project-panel-button').click()");
    await viewport(1440);
    await evaluate("document.querySelector('#theme-button').click()");
    await waitFor(`document.documentElement.dataset.theme === '${theme === "dark" ? "light" : "dark"}'`, "theme switch");
  }
  await viewport(390);
  await checkLayout();
  await captureScreenshot("brand-ide-mobile");
  assert.deepEqual(runtimeExceptions, [], "No browser runtime exceptions");
  console.log("OpenAEC site and IDE themes: fonts, responsive layouts, navigation, theme switching and text contrast passed.");
} finally {
  await closeBrowserSession();
}
