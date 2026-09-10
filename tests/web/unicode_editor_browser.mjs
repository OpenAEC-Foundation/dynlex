import assert from "node:assert/strict";
import {navigate,waitFor,evaluate,dispatchKey,captureScreenshot,runtimeExceptions,consoleErrors,closeBrowserSession} from "./browser_test_driver.mjs";

await navigate("/farm.css");
await waitFor("location.pathname==='/farm.css'","same-origin editor fixture");
await evaluate(`(async () => {
  const {createDynLexEditor,DynLexConnection}=await import('/editor.js');
  document.body.innerHTML='<div id="unicode-editor" style="width:850px;height:330px"></div>';
  const worker=new Worker('/compiler/compiler-worker.js',{type:'module'}), pending=new Map();
  let id=0, queue=Promise.resolve();
  worker.onmessage=({data})=>{
    const entry=pending.get(data.id); pending.delete(data.id);
    if(data.ok)entry.resolve(data.payload);else entry.reject(new Error(data.error));
  };
  const call=(type,payload={})=>{
    const result=queue.then(()=>new Promise((resolve,reject)=>{const request=++id;pending.set(request,{resolve,reject});worker.postMessage({id:request,type,payload});}));
    queue=result.catch(()=>{});return result;
  };
  await call('init');
  const connection=new DynLexConnection(message=>call('lsp.exchange',{message}));
  const source='to show {value} twice:\\n    @intrinsic("discard", value)\\nto example:\\n    show "😀é中" twice\\nexample';
  const editor=createDynLexEditor(document.querySelector('#unicode-editor'),{value:source,uri:'file:///workspace/unicode.dl'});
  const features=await editor.connect({connection});
  window.unicodeFixture={editor,connection,features,call,worker,source};
})()`);
const request=(method,params)=>evaluate(`unicodeFixture.connection.session.request(${JSON.stringify(method)},${JSON.stringify(params)})`);
const document={uri:"file:///workspace/unicode.dl"};
const source=await evaluate("unicodeFixture.source"), line=source.split("\n")[3];
const calls=await request("dynlex/callExpressions",document);
assert.deepEqual(calls.find(call=>call.range.start.line===3).range.end,{line:3,character:line.length});
const symbols=await request("textDocument/documentSymbol",{textDocument:document});
assert.deepEqual(symbols.find(symbol=>symbol.name==="example").range.end,{line:3,character:line.length});
const hover=await request("textDocument/hover",{textDocument:document,position:{line:3,character:line.length-1}});
assert.equal(hover.range.end.character,line.length);
await evaluate("unicodeFixture.editor.editor.executeEdits('test',[{range:{startLineNumber:4,startColumn:13,endLineNumber:4,endColumn:15},text:'Å'}]);");
await evaluate("unicodeFixture.features.commitActiveLine()");
const changed=await request("dynlex/readDocument",document);
assert.equal(changed,source.replace("é中","Å"));
const tokens=await request("textDocument/semanticTokens/full",{textDocument:document});
let row=0,column=0;
for(let offset=0;offset<tokens.data.length;offset+=5) {
  const [dl,dc,length]=tokens.data.slice(offset,offset+5);row+=dl;column=dl?dc:column+dc;
  assert.ok(column+length<=changed.split("\n")[row].length,"Semantic tokens stay within UTF-16 lines");
}
await evaluate(`(() => {
  const {editor}=unicodeFixture; editor.setValue(${JSON.stringify(source.replace("é中","Å").replace('" twice\nexample','" tw\nexample'))});
  editor.editor.setPosition({lineNumber:4,column:editor.model.getLineMaxColumn(4)});editor.focus();editor.editor.trigger('test','editor.action.triggerSuggest',{});
})()`);
await waitFor("[...document.querySelectorAll('.suggest-widget.visible .monaco-list-row')].some(row=>row.textContent.includes('twice'))","completion after emoji");
await dispatchKey("Tab","Tab",9);
assert.match(await evaluate("unicodeFixture.editor.getValue()"),/show "😀Å" twice/);
await captureScreenshot("unicode-editor");
const invalid='to show {value}: @intrinsic("discard", value)\nshow "😀é中" unknown';
const compiled=await evaluate(`unicodeFixture.call('compile',{source:${JSON.stringify(invalid)},version:1})`);
assert.notEqual(compiled.status,0);
assert.equal(compiled.diagnostics[0].range.end.column,invalid.split("\n")[1].length+1);
await evaluate("(async()=>{await unicodeFixture.features.stop();await unicodeFixture.connection.stop();unicodeFixture.editor.dispose();unicodeFixture.worker.terminate();})()");
assert.deepEqual(runtimeExceptions,[]);assert.deepEqual(consoleErrors,[]);
console.log("Browser compiler: Unicode edits, semantic ranges, completions, hover, full symbol bodies and compile diagnostics passed.");
await closeBrowserSession();
