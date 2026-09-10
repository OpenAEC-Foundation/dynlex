import assert from 'node:assert/strict';
import {navigate,waitFor,clickElement,command,dispatchKey,evaluate,runtimeExceptions,consoleErrors,closeBrowserSession} from './browser_test_driver.mjs';
import {withEditor,readCode} from './editor_test_driver.mjs';

async function assertEnter(host,source,indentation) {
  await evaluate(`document.querySelector(${JSON.stringify(host)}).scrollIntoView({block:'center',behavior:'instant'})`);
  await withEditor(host,`model.setValue(${JSON.stringify(source)});editor.focus();editor.setPosition({lineNumber:model.getLineCount(),column:model.getLineMaxColumn(model.getLineCount())});`);
  await dispatchKey('Enter','Enter',13,0,'\r');
  assert.equal(await readCode(host),source+'\n'+indentation,`${host}: Enter after ${JSON.stringify(source)}`);
}

await navigate('/');
await waitFor("document.querySelectorAll('[data-snippet-source][data-language-ready=true]').length===5",'shared editors ready');
await assertEnter('[data-snippet-source]','loop forever:','    ');
await clickElement('[data-river-challenge-load]');
await waitFor("document.querySelector('[data-river-editor-shell]')?.dataset.languageReady==='true'",'river editor ready');
await assertEnter('[data-river-editor-shell]','loop forever:','    ');
await clickElement('[data-farm-challenge-load]');
await waitFor("document.querySelector('[data-farm-editor]')?.dataset.languageReady==='true'",'farm editor ready');
const host='[data-farm-editor]';
await evaluate("document.querySelector('[data-farm-editor]').scrollIntoView({block:'center',behavior:'instant'})");
await withEditor(host,"model.setValue('');editor.focus();editor.setPosition({lineNumber:1,column:1});");
await command('Input.insertText',{text:'loop forever:'});
await dispatchKey('Enter','Enter',13,0,'\r');
assert.equal(await readCode(host),'loop forever:\n    ','Enter indents the loop body by one level');
await command('Input.insertText',{text:'if i can reach it:'});
await dispatchKey('Enter','Enter',13,0,'\r');
assert.equal(await readCode(host),'loop forever:\n    if i can reach it:\n        ','Nested sections add another indentation level');
await command('Input.insertText',{text:'wait'});
await dispatchKey('Enter','Enter',13,0,'\r');
assert.equal(await readCode(host),'loop forever:\n    if i can reach it:\n        wait\n        ','Ordinary statements keep the current indentation');
await assertEnter(host,'loop forever: # keep working','    ');
await assertEnter(host,'    # instructions:','    ');
await assertEnter(host,'    wait # instructions:','    ');
await assertEnter(host,'    print "hello:"','    ');
await assertEnter(host,'if "a:#b" is "a:#b":','    ');
await navigate('/ide/');
await waitFor("document.querySelector('#editor')?.dataset.editorReady==='true'",'full IDE editor ready');
await assertEnter('#editor','loop forever:','    ');
assert.deepEqual(runtimeExceptions,[]);assert.deepEqual(consoleErrors,[]);
console.log('Enter indents sections across examples, both games and the IDE, keeps nested indentation, and ignores colons inside comments and strings.');
await closeBrowserSession();
