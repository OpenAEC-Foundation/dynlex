import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { navigate, waitFor, evaluate, clickElement, captureScreenshot, runtimeExceptions, consoleErrors, closeBrowserSession } from "./browser_test_driver.mjs";
import { editCode, withEditor } from "./editor_test_driver.mjs";

const colors = JSON.parse(await readFile(new URL("../../shared/call-colors.json", import.meta.url), "utf8"));
const rgb = hex => `rgb(${hex.match(/../g).map(part => parseInt(part, 16)).join(", ")})`;
await navigate("/");
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5", "shared editors connected");
await clickElement("[data-farm-challenge-load]");
await waitFor("document.querySelector('[data-farm-editor]')?.dataset.languageReady==='true'", "farm editor connected");
await evaluate("document.querySelector('[data-farm-editor]').scrollIntoView({block:'center',behavior:'instant'})");
const barnColors = `(() => {
  const line = [...document.querySelectorAll('[data-farm-editor] .view-line')].find(line => line.textContent.replaceAll('\\u00a0', ' ').trim() === 'walk to the barn');
  return line ? [...line.querySelectorAll('span')].filter(span => !span.children.length && span.textContent.trim()).map(span => ({text: span.textContent.replaceAll('\\u00a0', ' ').trim(), color: getComputedStyle(span).color})) : [];
})()`;
await waitFor(`${barnColors}.some(span => span.text === 'the barn' && span.color === ${JSON.stringify(rgb(colors[1].dark))})`, "farm destination has its own nesting color");
assert.deepEqual(await evaluate(barnColors), [
  {text: 'walk to', color: rgb(colors[0].dark)}, {text: 'the barn', color: rgb(colors[1].dark)}
]);
await captureScreenshot("farm-barn-call-colors");
await editCode('[data-farm-editor]', 'walk to the nearest cow');
const cowColors = barnColors.replace("walk to the barn", "walk to the nearest cow");
await waitFor(`${cowColors}.some(span => span.text === 'cow' && span.color === ${JSON.stringify(rgb(colors[2].dark))})`, "nearest cow is a nested kind argument");
assert.deepEqual(await evaluate(cowColors), [
  {text: 'walk to', color: rgb(colors[0].dark)}, {text: 'the nearest', color: rgb(colors[1].dark)}, {text: 'cow', color: rgb(colors[2].dark)}
]);
await captureScreenshot("farm-nearest-cow-call-colors");
const selector = "[data-snippet-source]";
await editCode(selector, `one means: 1
{value} doubled means: @intrinsic("add", value, value)
to show {value}: @intrinsic("discard", value)
show one doubled doubled`);
await withEditor(selector, "editor.updateOptions({lineNumbers:'on'});editor.layout({width:650,height:180});editor.revealLine(4);");
await evaluate(`document.querySelector(${JSON.stringify(selector)}).scrollIntoView({block:'center',behavior:'instant'})`);
const sample = `(() => [...document.querySelectorAll('${selector} .view-line')].find(line => line.textContent.replaceAll('\\u00a0',' ').startsWith('show one'))?.querySelectorAll('span'))()`;
const measurements = `(() => {const spans=${sample};return spans?[...spans].filter(s=>s.children.length===0 && s.textContent.trim()).map(s=>({text:s.textContent.replaceAll('\\u00a0',' ').trim(),color:getComputedStyle(s).color})):[]})()`;
for (const theme of ["dark", "light"]) {
  await withEditor(selector, `(await import('/editor.js')).monaco.editor.setTheme('dynlex-${theme}');`);
  await waitFor(`${measurements}.filter(s=>s.color===${JSON.stringify(rgb(colors[3][theme]))}).length>0`, `${theme} nested call colors`);
  const spans = await evaluate(measurements);
  assert.deepEqual(spans, [
    { text:"show", color:rgb(colors[0][theme]) }, { text:"one", color:rgb(colors[3][theme]) },
    { text:"doubled", color:rgb(colors[2][theme]) }, { text:"doubled", color:rgb(colors[1][theme]) }
  ]);
  await captureScreenshot(`call-colors-${theme}`);
}
assert.deepEqual(runtimeExceptions, []);
assert.deepEqual(consoleErrors, []);
console.log("Real compiler nesting colors render distinctly in both shared editor themes.");
await closeBrowserSession();
