import assert from 'node:assert/strict';
import {withEditor,editCode,readCode} from './editor_test_driver.mjs';
const host='[data-river-editor-shell]';
export async function assertRiverIncrementalHighlighting({evaluate,starterSource,waitFor}) {
  await waitFor("[...document.querySelectorAll('[data-river-editor-shell] .view-line:first-child span')].some(s=>getComputedStyle(s).fontStyle==='italic')",'highlighted starter comment');
  const before=await evaluate("document.querySelector('[data-river-editor-shell] .view-line').innerHTML");
  await withEditor(host, `editor.focus();editor.setPosition({lineNumber:2,column:8});editor.executeEdits('test',[{range:{startLineNumber:2,startColumn:9,endLineNumber:2,endColumn:12},text:'sheep'}]);`);
  assert.equal(await evaluate("document.querySelector('[data-river-editor-shell] .view-line').innerHTML"),before,'Editing a command preserves the comment highlighting');
  await editCode(host,starterSource);
  assert.equal(await readCode(host),starterSource);
}
