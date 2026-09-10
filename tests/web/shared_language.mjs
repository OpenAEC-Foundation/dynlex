import assert from 'node:assert/strict';
import {SourceCoordinates} from '../../src/web/ide/src/sourceCoordinates.js';
import {DynLexConnection} from '../../src/web/ide/src/languageConnection.js';
import {DynLexLanguageFeatures} from '../../src/web/ide/src/lspIntegration.js';
const prefix=new SourceCoordinates('file:///workspace/game.dl','import lib/farm_challenge.dl\n\n');
const point={line:0,character:4};
assert.deepEqual(prefix.map({position:point},1),{position:{line:2,character:4}});
assert.deepEqual(prefix.map({uri:'file:///lib/std.dl',range:{start:point,end:point}},-1).range.start,point);
assert.deepEqual(prefix.tokens([0,0,6,0,0,2,0,4,1,0,0,5,3,2,0]),[0,0,4,1,0,0,5,3,2,0]);
const messages=[],providers=[],completionProviders=[],markers=[];
let completeTokens=null, pauseOpen=false, opened=null;
const connection=new DynLexConnection(async message=>{
 messages.push(message);
 if(message.method==='initialize') return [{jsonrpc:'2.0',id:message.id,result:{capabilities:{completionProvider:{triggerCharacters:[' ']},semanticTokensProvider:{legend:{tokenTypes:['function'],tokenModifiers:[]}}}}}];
 if(message.method==='textDocument/completion') return [{jsonrpc:'2.0',id:message.id,result:{items:[{
  label:'collect ',kind:14,textEdit:{range:{start:{line:2,character:0},end:{line:2,character:4}},newText:'collect '},
  command:{title:'Continue completion',command:'editor.action.triggerSuggest'}
 }]}}];
 if(message.method==='textDocument/didOpen' && pauseOpen) await new Promise(resolve=>opened=resolve);
 if(message.method==='textDocument/semanticTokens/full') {
  await new Promise(resolve=>completeTokens=resolve);
  return [{jsonrpc:'2.0',id:message.id,result:{data:[2,0,4,0,0]}}];
 }
 if(message.id) return [{jsonrpc:'2.0',id:message.id,result:null}];
 return [];
});
const noop=()=>({dispose(){}});
const monaco={
 Emitter:class {event=noop;fire(){}dispose(){}},
 languages:{CompletionItemKind:{Keyword:17},
 registerCompletionItemProvider(selector,provider){completionProviders.push({selector,provider});return noop();},
 registerDocumentSemanticTokensProvider(selector,provider){providers.push({selector,provider});return noop();}},
 editor:{registerEditorOpener:noop,setModelMarkers(model,owner,values){markers.push({model,values});}},
 MarkerSeverity:{Error:8}
};
function fixture(name){
 const model={version:1,text:'wait',uri:{path:`/workspace/${name}.dl`,toString:()=>`file:///workspace/${name}.dl`},
 getVersionId(){return this.version;},getValue(){return this.text;},getWordUntilPosition:()=>({startColumn:1}),onDidChangeContent:noop,getLanguageId:()=> 'dynlex',isDisposed:()=>false};
 const editor={getModel:()=>model,getPosition:()=>({lineNumber:1,column:1}),hasTextFocus:()=>false,
 onDidChangeModel:noop,onDidChangeCursorPosition:noop,onDidFocusEditorText:noop,onDidBlurEditorText:noop};
 return {model,features:new DynLexLanguageFeatures({monaco,editor,mainModel:model,connection,embedded:true,prefix:'import lib/farm_challenge.dl\n\n'})};
}
const a=fixture('a'),b=fixture('b');
await a.features.start();await b.features.start();
assert.equal(messages.filter(m=>m.method==='initialize').length,1,'All editors share one initialized LSP session');
assert.notDeepEqual(providers[0].selector,providers[1].selector,'Language providers target their own document');
assert.equal(messages.find(m=>m.method==='textDocument/didOpen').params.textDocument.text,'import lib/farm_challenge.dl\n\nwait');
const completion=await completionProviders[0].provider.provideCompletionItems(a.model,{lineNumber:1,column:5},{},{isCancellationRequested:false});
assert.deepEqual(completion.suggestions[0].command,{id:'editor.action.triggerSuggest',title:'Continue completion'},'Completion acceptance must request the next suggestions');
assert.deepEqual(completion.suggestions[0].range,{startLineNumber:1,startColumn:1,endLineNumber:1,endColumn:5});
let result=providers[0].provider.provideDocumentSemanticTokens(a.model,null,{isCancellationRequested:false});
await new Promise(resolve=>setTimeout(resolve,0));a.model.version++;completeTokens();
await assert.rejects(result,{name:'Canceled'},'A stale reply must cancel; a null reply clears Monaco’s existing colors');
result=providers[0].provider.provideDocumentSemanticTokens(a.model,null,{isCancellationRequested:true});
await new Promise(resolve=>setTimeout(resolve,0));completeTokens();
await assert.rejects(result,{name:'Canceled'},'Cancellation must preserve the current token document');
result=providers[0].provider.provideDocumentSemanticTokens(a.model,null,{isCancellationRequested:false});
await new Promise(resolve=>setTimeout(resolve,0));completeTokens();
assert.deepEqual([...((await result).data)],[0,0,4,0,0]);
for(const listener of connection.diagnostics) listener({uri:a.model.uri.toString(),diagnostics:[{range:{start:{line:2,character:0},end:{line:2,character:4}},message:'example',severity:1}]});
assert.equal(markers.find(m=>m.model===a.model).values[0].startLineNumber,1);
assert.equal(markers.find(m=>m.model===b.model).values.length,0);
await a.features.stop();assert.equal(connection.session.state,'running');
assert.equal(connection.session.documents.has(b.model.uri.toString()),true,'Closing one editor keeps other documents open');
await b.features.stop();
pauseOpen=true;
const c=fixture('c'), starting=c.features.start();
while(!opened) await new Promise(resolve=>setTimeout(resolve,0));
c.model.text='turn left';c.model.version++;
opened();await starting;
assert.equal(connection.session.documents.get(c.model.uri.toString()).text,'import lib/farm_challenge.dl\n\nturn left','Edits made during language startup are synchronized before providers run');
await c.features.stop();await connection.stop();
console.log('Shared session lifecycle, per-document providers, prefix coordinates, and stale/cancelled semantic replies passed.');
