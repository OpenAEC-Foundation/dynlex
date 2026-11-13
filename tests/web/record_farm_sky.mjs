// Explicit recording command. Normal tests never regenerate reference values.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {navigate,evaluate,closeBrowserSession,consoleErrors,runtimeExceptions} from './browser_test_driver.mjs';

const output=process.env.DYNLEX_FARM_SKY_REFERENCE_OUTPUT;
assert(output,'Set DYNLEX_FARM_SKY_REFERENCE_OUTPUT to the reference JSON destination');
const fragment=await readFile(new URL('./references/farm_sky.frag',import.meta.url),'utf8');
const directions=[[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1],[1,1,1],[-.5,.9,.2],[-.5,.9,.25],[-.65,.9,.2],[-.45,.9,.2],[0,.1,1],[0,.3,1],[0,-.1,1]];
function record(fragment,directions) {
  const gl=document.createElement('canvas').getContext('webgl2');
  if(!gl||!gl.getExtension('EXT_color_buffer_float')) throw new Error('The reference requires float WebGL2 render targets');
  const compile=(kind,text)=>{
    const shader=gl.createShader(kind);gl.shaderSource(shader,text);gl.compileShader(shader);
    if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
    return shader;
  };
  const vertex=compile(gl.VERTEX_SHADER,'#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID==1)?3.0:-1.0,(gl_VertexID==2)?3.0:-1.0);gl_Position=vec4(p,0,1);}');
  const pixel=compile(gl.FRAGMENT_SHADER,fragment),program=gl.createProgram();
  gl.attachShader(program,vertex);gl.attachShader(program,pixel);gl.linkProgram(program);
  if(!gl.getProgramParameter(program,gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);
  gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA32F,1,1,0,gl.RGBA,gl.FLOAT,null);
  const framebuffer=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);
  gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
  if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE) throw new Error('Incomplete sky reference framebuffer');
  gl.useProgram(program);gl.viewport(0,0,1,1);
  try {
    return directions.map(direction=>{
      gl.uniform3fv(gl.getUniformLocation(program,'skyDirection'),direction);gl.drawArrays(gl.TRIANGLES,0,3);
      const color=new Float32Array(4);gl.readPixels(0,0,1,1,gl.RGBA,gl.FLOAT,color);
      if(gl.getError()!==gl.NO_ERROR||[...color].some(value=>!Number.isFinite(value))) throw new Error('Invalid sky reference pixel');
      return {direction,color:[...color].slice(0,3)};
    });
  } finally {gl.deleteFramebuffer(framebuffer);gl.deleteTexture(texture);gl.deleteProgram(program);gl.deleteShader(vertex);gl.deleteShader(pixel);}
}
try {
  await navigate('/');
  const cases=await evaluate(`(${record.toString()})(${JSON.stringify(fragment)},${JSON.stringify(directions)})`);
  assert.equal(cases.length,directions.length);assert.deepEqual(consoleErrors,[]);assert.deepEqual(runtimeExceptions,[]);
  await writeFile(output,JSON.stringify({source:'Original Farm sky fragment',sha256:createHash('sha256').update(fragment).digest('hex'),cases},null,2)+'\n');
  console.log(`Recorded ${cases.length} original Farm sky pixels in floating point.`);
} finally {closeBrowserSession();}
