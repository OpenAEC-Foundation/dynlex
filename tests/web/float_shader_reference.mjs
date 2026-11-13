// Execute preserved reference GLSL directly, independently of DynLex translation.
// Kept self-contained so its source can run in a private browser via CDP.
export function renderFloatShaderReferences(fragment,uniforms,samples) {
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
  if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE) throw new Error('Incomplete reference framebuffer');
  gl.useProgram(program);gl.viewport(0,0,1,1);
  try {
    return samples.map(sample=>{
      for(const {name,type} of uniforms) {
        const location=gl.getUniformLocation(program,name);
        if(type==='vec3') gl.uniform3fv(location,sample[name]);
        else if(type==='float') gl.uniform1f(location,sample[name]);
        else if(type==='int') gl.uniform1i(location,sample[name]);
        else throw new Error('Unsupported reference uniform type');
      }
      gl.drawArrays(gl.TRIANGLES,0,3);
      const color=new Float32Array(4);gl.readPixels(0,0,1,1,gl.RGBA,gl.FLOAT,color);
      if(gl.getError()!==gl.NO_ERROR||[...color].some(value=>!Number.isFinite(value))) throw new Error('Invalid reference pixel');
      return [...color].slice(0,3);
    });
  } finally {gl.deleteFramebuffer(framebuffer);gl.deleteTexture(texture);gl.deleteProgram(program);gl.deleteShader(vertex);gl.deleteShader(pixel);}
}
