// Independent Unicode oracle; the production matcher uses KMP and static tables.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';
const root=path.resolve(import.meta.dirname,'../..');
const compiler=path.resolve(process.argv[2]??path.join(root,'build/dynlex'));
const fold=new Map(),word=new Uint8Array(0x110000);
const data=path.join(root,'data/unicode/17.0.0');
for(const line of (await fs.readFile(path.join(data,'CaseFolding.txt'),'utf8')).split('\n')) {
  const found=/^([0-9A-F]+); ([CS]); ([0-9A-F]+);/.exec(line);
  if(found)fold.set(parseInt(found[1],16),parseInt(found[3],16));
}
for(const line of (await fs.readFile(path.join(data,'DerivedGeneralCategory.txt'),'utf8')).split('\n')) {
  const found=/^([0-9A-F]+)(?:\.\.([0-9A-F]+))?\s*; ([A-Za-z]+)/.exec(line);
  if(found && (/^[LMN]/.test(found[3]) || found[3]==='Pc'))
    word.fill(1,parseInt(found[1],16),parseInt(found[2]??found[1],16)+1);
}
word[0x200c]=word[0x200d]=1;
const folded=code=>fold.get(code)??code;
function expected({text,query,sensitive,whole}) {
  const characters=[];let bytes=0;
  for(const value of text) {
    const end=bytes+Buffer.byteLength(value);
    characters.push({code:value.codePointAt(0),start:bytes,end});bytes=end;
  }
  const wanted=Array.from(query,value=>sensitive?value.codePointAt(0):folded(value.codePointAt(0)));
  const found=[];
  if(wanted.length===0)return found;
  for(let start=0;start+wanted.length<=characters.length;) {
    const end=start+wanted.length;
    const equal=wanted.every((code,index)=>code===(sensitive?characters[start+index].code:folded(characters[start+index].code)));
    const boundary=!whole || ((!start || !word[characters[start-1].code]) && (end===characters.length || !word[characters[end].code]));
    if(equal && boundary) {
      found.push([characters[start].start,characters[end-1].end]);start=end;
    } else start++;
  }
  return found;
}
const cases=[];
for(const [source,target] of fold) {
  for(const sensitive of [false,true])cases.push({text:String.fromCodePoint(source)+' '+String.fromCodePoint(target),query:String.fromCodePoint(target),sensitive,whole:false});
}
let seed=0xd1e17;
const random=limit=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed%limit;};
const alphabet=Array.from('abcABCkKKΣσς𐐀𐐨ßẞéé🙂中_ \n\t\0İıकाची\u200c\u200d');
for(let iteration=0;iteration<1000;iteration++) {
  const text=Array.from({length:random(49)},()=>alphabet[random(alphabet.length)]).join('');
  let query;
  if(iteration%2 && text.length) {
    const scalars=Array.from(text),start=random(scalars.length);
    query=scalars.slice(start,start+1+random(6)).join('');
  } else query=Array.from({length:random(9)},()=>alphabet[random(alphabet.length)]).join('');
  for(const sensitive of [false,true])for(const whole of [false,true])cases.push({text,query,sensitive,whole});
}
const oracle=cases.map(expected), input=JSON.stringify(cases);
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-text-search-'));
async function runWasm(file,label) {
  const bytes=await fs.readFile(file),filesystem=createRuntimeFilesystem();
  filesystem.files.set('input.search',{data:new TextEncoder().encode(input),modificationTime:filesystem.lastTimestamp});
  let output='';
  const runtime=buildRuntimeImports({push:chunk=>{output+=chunk;}},filesystem,inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,64*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);
  instance.exports.main();check(output,label);
  assert.equal(allocation.live,0,`${label}: retained search allocations`);
  console.log(`${label}: ${allocation.peak} peak live bytes, zero retained`);
}
function check(output,label) {
  const actual=JSON.parse(output);assert.equal(actual.length,oracle.length,label);
  for(let index=0;index<oracle.length;index++)assert.deepEqual(actual[index],oracle[index],`${label}: ${index}: ${JSON.stringify(cases[index])}`);
  console.log(`PASS literal search ${label}: ${cases.length} independent Unicode cases`);
}
try {
  await fs.writeFile(path.join(directory,'input.search'),input);
  const entry='tests/web/text_search_model.dl';
  for(const level of ['O0','O2']) {
    const binary=path.join(directory,'search.out');
    execFileSync(compiler,[entry,'-'+level,'-o',binary],{cwd:root,stdio:'inherit',timeout:30000});
    check(execFileSync(binary,{cwd:directory,encoding:'utf8',timeout:30000,maxBuffer:8*1024*1024}),`native ${level}`);
    const wasm=path.join(directory,'search.wasm');
    execFileSync(compiler,[entry,'-'+level,'--emit-wasm','-o',wasm],{cwd:root,stdio:'inherit',timeout:30000});
    await runWasm(wasm,`Wasm ${level}`);
  }
  if(process.argv.includes('--shipped')) {
    const request=path.join(directory,'request.json');
    await fs.writeFile(request,JSON.stringify({sources:{[entry]:await fs.readFile(path.join(root,entry),'utf8')},jobs:[{entry,kind:'wasm',output:'shipped.wasm'}]}));
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:60000});
    await runWasm(path.join(directory,'shipped.wasm'),'shipped compiler/Wasm');
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
