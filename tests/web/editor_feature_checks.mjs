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

// Definition visits use the same view and document lifecycle in both embeddings.
export async function verifySourceDefinition({command,evaluate,waitFor,key,capture,source,line,column,filename,name}) {
  const location=await evaluate('location.href');
  await key('Home','Home',36,2);
  for(let row=0;row<line;row++) await key('ArrowDown','ArrowDown',40);
  await key('Home','Home',36);
  for(let character=0;character<column;character++) await key('ArrowRight','ArrowRight',39);
  await key('F12','F12',123);
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify(filename+' · Read only')})`);
  await capture(name+'-definition');
  await key('ArrowLeft','ArrowLeft',37,1);
  await waitFor(`!window.siteProbe.visible.includes(${JSON.stringify(filename+' · Read only')}) && document.querySelector('textarea').value===${JSON.stringify(source)}`);
  assert.equal(await evaluate('location.href'),location,'Editor Back changed the browser location.');
  console.log('Shared definition and return passed:',name);
}

export async function verifySourceHover({command,evaluate,waitFor,key,capture,source,name}) {
  const inspected='@intrinsic("store", answer, 7)\n@intrinsic("discard", answer)';
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:inspected});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(inspected)}`);
  await waitFor('window.colorProbe.some(item=>item.text.trim()==="answer")');
  const point=await evaluate('window.colorProbe.filter(item=>item.text.trim()==="answer").at(-1)');
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:point.x,y:point.y});
  await waitFor('window.siteProbe.visible.includes("a 32-bit integer") && window.siteProbe.visible.includes("value: 7")');
  assert(await evaluate('document.activeElement===document.querySelector("textarea")'),
    'Hover inspection took keyboard focus away from the source.');
  await capture(name+'-hover');
  await key('ArrowLeft','ArrowLeft',37);
  await waitFor(`document.querySelector('textarea').selectionStart===${inspected.length-1}`);
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared hover and keyboard ownership passed:',name);
}
