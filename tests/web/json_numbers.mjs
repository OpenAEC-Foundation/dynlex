// Independent binary64 oracle: JavaScript's correctly rounded decimal conversion.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {buildRuntimeImports,createRuntimeFilesystem,inspectRuntimeWasmLayout} from '../../src/web/ide/public/compiler/runtimeImports.js';
import {observeAllocations} from './allocation_probe.mjs';
import {nativeText} from './native_text.mjs';
const root=path.resolve(import.meta.dirname,'../..');
const directory=await fs.mkdtemp(path.join(os.tmpdir(),'dynlex-json-numbers-'));
const memory=new DataView(new ArrayBuffer(8));
const number=bits=>{memory.setBigUint64(0,bits,true);return memory.getFloat64(0,true);};
const bits=value=>{memory.setFloat64(0,value,true);return memory.getBigUint64(0,true);};
const values=new Set([0n,1n,2n,3n,0x000fffffffffffffn,0x0010000000000000n,
  0x0010000000000001n,0x3ff0000000000000n,0x7fefffffffffffffn,0x8000000000000000n]);
for(let exponent=1n;exponent<2047n;exponent+=31n) {
  const power=exponent<<52n;
  for(const offset of [-1n,0n,1n]) for(const sign of [0n,1n<<63n]) values.add((power+offset)|sign);
}
let state=0x123456789abcdefn;
for(let index=0;index<256;index++) {
  state=BigInt.asUintN(64,state^(state<<13n));state^=state>>7n;state=BigInt.asUintN(64,state^(state<<17n));
  if(Number.isFinite(number(state))) values.add(state);
}
for(const value of [0.1,0.3,0.1+0.2,((6-.18)-.18)-.18,1e-7,1e-6,1e20,1e21,1e23,1.0000000000000002,
  Math.fround(0.1),Math.fround(-1.23),Math.fround(1e-45),Math.fround(3.4e38)]) values.add(bits(value));
const samples=[...values];
const decimals=['0.3','-0','1e309','-1e309','1e-325','-1e-325','1e9999999999999999999999',
  '-1e-9999999999999999999999','0e999999999999999999999999','1'+'0'.repeat(1800)+'1e-1800',
  '0.'+'0'.repeat(800)+'1e800'];
for(const sample of samples.filter((_,index)=>index%7===0)) {
  const value=number(sample);decimals.push(value.toPrecision(100),value.toExponential(50));
}
function dyadic(mantissa,power) {
  if(power>=0) return (mantissa<<BigInt(power)).toString();
  const places=-power,digits=(mantissa*5n**BigInt(places)).toString();
  return digits.length>places?digits.slice(0,-places)+'.'+digits.slice(-places):'0.'+'0'.repeat(places-digits.length)+digits;
}
function parts(bits) {
  const exponent=Number((bits>>52n)&2047n),fraction=bits&((1n<<52n)-1n);
  return {mantissa:exponent===0?fraction:fraction+(1n<<52n),power:exponent===0?-1074:exponent-1075};
}
// Exact halfway values and numbers just beyond those ties exercise both parity
// choices, zero/subnormal transitions and unequal spacing at binade boundaries.
for(const lower of [0n,1n,2n,0x000fffffffffffffn,0x0010000000000000n,
  0x3fefffffffffffffn,0x3ff0000000000000n,0x4000000000000000n,0x7feffffffffffffen,0x7fefffffffffffffn]) {
  const a=parts(lower),b=parts(lower+1n),power=Math.min(a.power,b.power)-1;
  const mantissa=(a.mantissa<<BigInt(a.power-power-1))+(b.mantissa<<BigInt(b.power-power-1));
  const midpoint=dyadic(mantissa,power);
  for(const text of [midpoint,midpoint+(midpoint.includes('.')?'':'.')+'0'.repeat(20)+'1']) decimals.push(text,'-'+text);
}
const source=`import lib/json_builders.dl
set samples to [${samples.map(value=>`(${value} as a 64 bit unsigned integer)`).join(',')}]
set index to 0
loop while index < the length of samples:
    set value to binary64 from bits (the item at index in samples)
    set node to a new JSON number from value
    print line the compact JSON form of node
    parse (node's text) as JSON and set parsed to it
    require condition ((the binary64 bits of (parsed's value's number)) = (the item at index in samples)) reporting "A serialized binary64 changed bits"
    free parsed
    free node
    increment index
set decimals to [${decimals.map(text=>`(the string form of ${JSON.stringify(text)})`).join(',')}]
set index to 0
loop while index < the length of decimals:
    parse (the item at index in decimals) as JSON and set parsed to it
    require condition (parsed succeeded) reporting "A generated decimal was rejected"
    print line the binary64 bits of (parsed's value's number)
    free parsed
    increment index
`;
const expected=[...samples.map(sample=>{
  const value=number(sample);return Object.is(value,-0)?'-0':String(value);
}),...decimals.map(text=>String(bits(Number(text))))];
function check(output,label) {
  assert.deepEqual(output.trimEnd().split('\n'),expected,label);
  console.log(`${label}: ${samples.length} shortest round trips and ${decimals.length} independent decimal parses pass.`);
}
async function runWasm(file,label) {
  const bytes=await fs.readFile(file),output=[];
  const runtime=buildRuntimeImports(output,createRuntimeFilesystem(),inspectRuntimeWasmLayout(bytes));
  const allocation=observeAllocations(runtime,4*1024*1024);
  const {instance}=await WebAssembly.instantiate(bytes,runtime);instance.exports.main();
  check(output.join(''),label);
  assert.equal(allocation.live,0,label+' retained decimal conversion buffers');
  console.log(label+`: ${allocation.peak} peak live bytes; all owned buffers freed.`);
}
try {
  const file=path.join(directory,'numbers.dl');await fs.writeFile(file,source);
  const compiler=path.resolve(process.argv[2]||'build/dynlex');
  for(const optimization of ['-O0','-O2']) {
    const binary=path.join(directory,process.platform==='win32'?'numbers.exe':'numbers.out');
    execFileSync(compiler,[file,optimization,'-o',binary],{cwd:root,stdio:'inherit',timeout:180000});
    check(nativeText(execFileSync(binary,{encoding:'utf8',timeout:30000})),'Native '+optimization);
  }
  const wasm=path.join(directory,'numbers.wasm');
  execFileSync(compiler,[file,'--emit-wasm','-o',wasm],{cwd:root,stdio:'inherit',timeout:180000});
  await runWasm(wasm,'Native compiler → Wasm');
  if(process.argv.includes('--shipped')) {
    const request=path.join(directory,'project.json');await fs.writeFile(request,JSON.stringify({sources:{'numbers.dl':source},jobs:[{entry:'numbers.dl',kind:'wasm',output:'numbers.wasm'}]}));
    execFileSync(process.execPath,[path.join(root,'scripts/compile_project.mjs'),request,directory],{cwd:root,stdio:'inherit',timeout:180000});
    await runWasm(wasm,'Shipped compiler → Wasm');
  }
} finally {await fs.rm(directory,{recursive:true,force:true});}
