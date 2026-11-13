// Explicit reference recording. These pixels never come from the DynLex port.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {navigate,evaluate,closeBrowserSession,consoleErrors,runtimeExceptions} from './browser_test_driver.mjs';
import {renderFloatShaderReferences} from './float_shader_reference.mjs';

const output=process.env.DYNLEX_FARM_MATERIAL_REFERENCE_OUTPUT;
assert(output,'Set DYNLEX_FARM_MATERIAL_REFERENCE_OUTPUT to the reference JSON destination');
const fragment=await readFile(new URL('./references/farm_materials.frag',import.meta.url),'utf8');
const base=[.78,.67,.49],normal=[0,1,0],samples=[];
for(const point of [[-1,.13,-.28],[1,-.04,.19],[-.3,1,-.47],[0,-1,0],[0,0,1],[-1,-.8,.7],[1,.5,-.4],[-1,.6,-.2],[-1,-.2,.2],[1,.3,.5],[.2,1,-.4]])
  samples.push({mode:0,point,base,normal,time:0});
for(let index=0;index<32;index++) {
  const angle=index*.19634954084936207;
  samples.push({mode:0,point:[-1,.13+.54*Math.cos(angle),-.28+.56*Math.sin(angle)],base,normal,time:0});
}
for(const point of [[0,0,0],[.5,.3,-.5],[-9.5,.3,9.5],[4.25,.3,-6.125]]) for(const time of [0,.25,4.5])
  samples.push({mode:1,point,base,normal,time});
for(const distance of [0,55,60,77.5,95,100,150]) samples.push({mode:2,point:[distance,0,0],base,normal:[.4,.5,.6],time:0});
const uniforms=[{name:'point',type:'vec3'},{name:'base',type:'vec3'},{name:'normal',type:'vec3'},{name:'time',type:'float'},{name:'mode',type:'int'}];
try {
  await navigate('/');
  const colors=await evaluate(`(${renderFloatShaderReferences.toString()})(${JSON.stringify(fragment)},${JSON.stringify(uniforms)},${JSON.stringify(samples)})`);
  assert.equal(colors.length,samples.length);assert.deepEqual(consoleErrors,[]);assert.deepEqual(runtimeExceptions,[]);
  const cases=samples.map((sample,index)=>({...sample,color:colors[index]}));
  await writeFile(output,JSON.stringify({source:'Original Farm coat, water-normal and fog formulas',sha256:createHash('sha256').update(fragment).digest('hex'),cases},null,2)+'\n');
  console.log(`Recorded ${cases.length} Farm material reference pixels in floating point.`);
} finally {closeBrowserSession();}
