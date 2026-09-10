import assert from 'node:assert/strict';
import { withEditor, readCode } from './editor_test_driver.mjs';
const host='[data-river-editor-shell]';
export async function assertRiverEnterCommitsLine({dispatchKey,waitFor}) {
  await withEditor(host, `
    const line=model.getLineCount();
    editor.executeEdits('test',[{range:{startLineNumber:line,startColumn:1,endLineNumber:line,endColumn:model.getLineMaxColumn(line)},text:'ro'}]);
    editor.focus();editor.setPosition({lineNumber:line,column:3});
    editor.trigger('test','editor.action.triggerSuggest',{});
  `);
  await waitFor("[...document.querySelectorAll('.suggest-widget.visible .monaco-list-row')].some(row=>row.textContent.includes('row'))",'row completion');
  await dispatchKey('Enter','Enter',13,0,'\r');
  assert.match(await readCode(host),/\nro\n$/,'Enter inserts a newline without accepting the selected completion');
}
