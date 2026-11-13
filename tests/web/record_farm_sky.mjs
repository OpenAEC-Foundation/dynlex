// Explicit recording command. Normal tests never regenerate reference values.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {navigate,evaluate,closeBrowserSession,consoleErrors,runtimeExceptions} from './browser_test_driver.mjs';
import {renderFloatShaderReferences} from './float_shader_reference.mjs';

const output=process.env.DYNLEX_FARM_SKY_REFERENCE_OUTPUT;
assert(output,'Set DYNLEX_FARM_SKY_REFERENCE_OUTPUT to the reference JSON destination');
const fragment=await readFile(new URL('./references/farm_sky.frag',import.meta.url),'utf8');
const directions=[[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1],[1,1,1],[-.5,.9,.2],[-.5,.9,.25],[-.65,.9,.2],[-.45,.9,.2],[0,.1,1],[0,.3,1],[0,-.1,1]];
try {
  await navigate('/');
  const colors=await evaluate(`(${renderFloatShaderReferences.toString()})(${JSON.stringify(fragment)},[{name:'skyDirection',type:'vec3'}],${JSON.stringify(directions.map(skyDirection=>({skyDirection})))})`);
  const cases=directions.map((direction,index)=>({direction,color:colors[index]}));
  assert.equal(cases.length,directions.length);assert.deepEqual(consoleErrors,[]);assert.deepEqual(runtimeExceptions,[]);
  await writeFile(output,JSON.stringify({source:'Original Farm sky fragment',sha256:createHash('sha256').update(fragment).digest('hex'),cases},null,2)+'\n');
  console.log(`Recorded ${cases.length} original Farm sky pixels in floating point.`);
} finally {closeBrowserSession();}
