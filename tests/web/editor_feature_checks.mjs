import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {setTimeout as pause} from 'node:timers/promises';

const callColors=JSON.parse(await fs.readFile(new URL('../../shared/call-colors.json',import.meta.url),'utf8'));

export async function verifySourceNavigationAppearance({waitFor,disabled}) {
  for(const label of ['Back','Main file']) {
    await waitFor(`(() => {
      const action=window.colorProbe.find(item=>item.text.trim()===${JSON.stringify(label)});
      const available=window.colorProbe.find(item=>item.text.trim()==='Outline');
      if(!action || !available) return false;
      return action.ink.every((value,index)=>${disabled?'value+0.01<available.ink[index]':'Math.abs(value-available.ink[index])<0.0001'});
    })()`);
  }
}

export async function verifySourceCallColor({waitFor,text,depth=0,theme='dark'}) {
  const channels=callColors[depth][theme].match(/../g).map(channel=>{
    const value=parseInt(channel,16)/255;
    return value<=0.04045?value/12.92:Math.pow((value+0.055)/1.055,2.4);
  });
  await waitFor(`window.colorProbe.some(item=>item.text.trim()===${JSON.stringify(text)} && item.ink.every((value,index)=>Math.abs(value-${JSON.stringify(channels)}[index])<0.0001))`);
}

export async function verifySourceCallColors({command,waitFor,key,capture,source,name}) {
  const program=await fs.readFile(new URL('./fixtures/editor_call_colors.dl',import.meta.url),'utf8');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:program});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(program)}`);
  for(const [text,depth] of [['show',0],['one',3],['doubled',2],['doubled',1]])
    await verifySourceCallColor({waitFor,text,depth});
  await capture(name+'-call-colors');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared compiler-derived nested-call colors passed:',name);
}

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
  await verifySourceNavigationAppearance({waitFor,disabled:false});
  await capture(name+'-definition');
  await key('ArrowLeft','ArrowLeft',37,1);
  await waitFor(`!window.siteProbe.visible.includes(${JSON.stringify(filename+' · Read only')}) && document.querySelector('textarea').value===${JSON.stringify(source)}`);
  assert.equal(await evaluate('location.href'),location,'Editor Back changed the browser location.');
  console.log('Shared definition and return passed:',name);
}

export async function verifySourceHover({command,evaluate,waitFor,key,capture,source,name}) {
  const inspected='@intrinsic("store", answer, 7)\n@intrinsic("discard", answer)';
  // Typing under the previous mouse position can open a declaration tooltip
  // over the next token before semantic coloring finishes. Start outside the
  // source so the intended mouse move reaches the inspected token itself.
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:1,y:1});
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:inspected});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(inspected)}`);
  await waitFor('window.colorProbe.some(item=>item.text.trim()==="answer")');
  const point=await evaluate('window.colorProbe.filter(item=>item.text.trim()==="answer").at(-1)');
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:point.x,y:point.y});
  try {
    await waitFor('window.siteProbe.visible.includes("a 32-bit integer") && window.siteProbe.visible.includes("value: 7")');
  } catch(error) {
    console.error('Source hover evidence:',await evaluate(`JSON.stringify({point:${JSON.stringify(point)},
      source:document.querySelector('textarea').value,visible:window.siteProbe.visible,
      tokens:window.colorProbe.filter(item=>item.text.trim()==='answer').slice(-6),
      requests:window.siteProbe.hoverRequests,replies:window.siteProbe.hoverReplies})`));
    await capture(name+'-hover-failure');throw error;
  }
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

export async function verifySourceInspection({command,evaluate,waitFor,key,capture,source,name}) {
  const program='function inspect value {value}:\n    execute:\n        @intrinsic("return", value)\n\n@intrinsic("discard", inspect value 7)\n@intrinsic("discard", inspect value "boat")';
  const outline='↑ ↓ choose · Enter open · Esc close';
  const instances='↑ ↓ choose · Enter select · Esc close';
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:program});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(program)}`);
  await key('F9','F9',120);
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify(outline)}) && !window.siteProbe.visible.includes('Finding sections…')`);
  await capture(name+'-outline');
  await key('Home','Home',36);
  await key('Enter','Enter',13);
  await waitFor(`document.querySelector("textarea").selectionStart===${program.indexOf('inspect value')}`);
  await key('Home','Home',36,2);
  for(let column=0;column<25;column++) await key('ArrowRight','ArrowRight',39);
  await key('F8','F8',119);
  const labels='window.siteProbe.visible.filter(text=>text.startsWith("inspect value {") && text.endsWith(":value}"))';
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify(instances)}) && (${labels}).length===2`);
  const options=await evaluate(labels);
  assert(options.some(label=>label.includes('byte')) && options.some(label=>label.includes('integer')),
    'The inferred-instance fixture did not expose both call types.');
  await capture(name+'-instances');
  await key('End','End',35);
  await key('Enter','Enter',13);
  await waitFor(`!window.siteProbe.visible.includes(${JSON.stringify(instances)})`);
  await waitFor('window.colorProbe.some(item=>item.text.trim()==="value")');
  const point=await evaluate('window.colorProbe.filter(item=>item.text.trim()==="value")[0]');
  await command('Input.dispatchMouseEvent',{type:'mouseMoved',x:point.x,y:point.y});
  const expected=options[1].match(/^inspect value \{(.+):value\}$/)[1];
  await waitFor(`window.siteProbe.visible.includes(${JSON.stringify(expected)})`);
  assert(await evaluate('document.activeElement===document.querySelector("textarea")'),
    'Instance inspection took keyboard focus away from the source.');
  await capture(name+'-selected-instance');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared outline and inferred-instance selection passed:',name);
}

export async function verifySourceFixes({command,evaluate,waitFor,key,capture,source,name}) {
  const program=await fs.readFile(new URL('./fixtures/editor_quickfix.dl',import.meta.url),'utf8');
  const conversion='@intrinsic("cast", value, @intrinsic("type", "int", 32))';
  const fixed=program.replace('    return value\n',`    return ${conversion}\n`);
  assert.notEqual(fixed,program,'The quick-fix fixture lost its conflicting return.');
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:program});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(program)}`);
  await key('Home','Home',36,2);
  for(let row=0;row<5;row++) await key('ArrowDown','ArrowDown',40);
  await key('Home','Home',36);
  for(let column=0;column<11;column++) await key('ArrowRight','ArrowRight',39);
  // The request may precede analysis; its diagnostic context must refresh at dispatch.
  await key('F7','F7',118);
  await waitFor('window.siteProbe.visible.includes("Convert returned value to a 32-bit integer")');
  await capture(name+'-fixes');
  const uri=await evaluate('window.siteProbe.diagnostics.findLast(item=>item.diagnostics.some(problem=>problem.data?.quickFixes?.some(fix=>fix.title==="Convert returned value to a 32-bit integer"))).uri');
  await evaluate('window.siteProbe.diagnostics=[]');
  await key('Enter','Enter',13);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(fixed)}`);
  await waitFor(`window.siteProbe.diagnostics.some(item=>item.uri===${JSON.stringify(uri)} && item.diagnostics.length===0)`);
  await key('KeyZ','z',90,2);
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(program)}`);
  await key('KeyA','a',65,2);
  await command('Input.insertText',{text:source});
  await waitFor(`document.querySelector('textarea').value===${JSON.stringify(source)}`);
  console.log('Shared quick fix, fresh analysis and undo passed:',name);
}
