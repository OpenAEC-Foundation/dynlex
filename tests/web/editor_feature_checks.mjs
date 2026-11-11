import assert from 'node:assert/strict';
import {setTimeout as pause} from 'node:timers/promises';

export async function verifySourcePaste({command,evaluate,waitFor,key,source}) {
  const menu = 'window.siteProbe.visible.includes("↑ ↓ choose  ·  Enter insert  ·  Esc close")';
  await key('KeyA','a',65,2);
  await evaluate(`(() => {
    const clipboardData = new DataTransfer();
    clipboardData.setData('text/plain', 'g');
    document.querySelector('textarea').dispatchEvent(new ClipboardEvent('paste', {clipboardData, cancelable:true}));
  })()`);
  await waitFor(`document.querySelector('textarea').value==='g'`);
  await pause(250);
  assert(!(await evaluate(menu)), 'Pasting one character opened automatic suggestions.');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  await pause(250);
  assert(!(await evaluate(menu)),
    'Pasting a complete program opened automatic suggestions over its actions.');
}

// Run the same interaction contract in Studio and the prefixed River editor.
export async function verifySourceCompletion({command,evaluate,waitFor,key,capture,source,prefix,name}) {
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await evaluate('window.siteProbe.completions=[]');
  await key('Space',' ',32,2);
  await waitFor('window.siteProbe.completions.length>0 && window.siteProbe.visible.includes("↑ ↓ choose  ·  Enter insert  ·  Esc close")');
  const items=await evaluate('window.siteProbe.completions.at(-1)');
  items.sort((a,b)=>(a.sortText??a.label)<(b.sortText??b.label)?-1:(a.sortText??a.label)>(b.sortText??b.label)?1:0);
  const item=items[0];
  assert(item.textEdit && !item.additionalTextEdits,'This fixture must have one protocol text edit.');
  const wrapped=prefix+source;
  const offset=position=>{
    let start=0;
    for(let line=0;line<position.line;line++)start=wrapped.indexOf('\n',start)+1;
    return start+position.character-prefix.length;
  };
  const start=offset(item.textEdit.range.start),end=offset(item.textEdit.range.end);
  assert(start>=0 && end<=source.length,'Completion attempted to edit hidden context.');
  const expected=source.slice(0,start)+item.textEdit.newText+source.slice(end);
  await capture(name+'-completions');
  await key('Enter','Enter',13);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(expected)}`);
  await key('Escape','Escape',27);
  await key('KeyZ','z',90,2);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared completion and undo passed:',name);
}
