import {createGpu} from '/packages/dynlex-ui/platform/browser/gpu.js';
import {createGraphicsServices} from '/packages/dynlex-ui/platform/browser/graphics-services.js';

// Execute the real DynLex graph and its uploads through the public host service.
export async function renderFarmScene(backend) {
  const data=await fetch('/scene-data.json').then(response=>response.json());
  const errors=[],gpu=await createGpu(error=>errors.push(error));
  if(gpu.backend!==backend) throw new Error(`Expected ${backend}; selected ${gpu.backend}`);
  const width=960,height=720,canvas=document.createElement('canvas');
  canvas.width=width;canvas.height=height;canvas.id=`farm-${backend}`;
  document.body.replaceChildren(canvas);
  const surface=gpu.surface(canvas),resources=new Map();
  const maximum=Math.max(...['vertices','colors','indices','instances','shadowInstances','draws','shadowDraws'].map(name=>data[name].length))*4;
  const memory=new WebAssembly.Memory({initial:Math.ceil(maximum/65536)+1});
  let sequence=100;
  const services=createGraphicsServices({gpu,surfaces:new Map([[1,surface]]),resources,id:()=>sequence++,memory,base:location.href});
  const {resource}=await services.request(data.program,{signal:new AbortController().signal},surface);
  const host=services.imports;
  const words=(values,receive)=>{new Uint32Array(memory.buffer,0,values.length).set(values);receive(0,values.length);};
  const uniform=input=>words(input.data,pointer=>host.ui_shader_uniform(1,resource,input.pass,0,input.binding,input.scalar,input.rows,input.columns,pointer,input.rows*16));
  const quad=[];
  for(const [x,y] of [[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]]) quad.push(x*width,y*height,x,y,1,1,1,1);
  const context=new OffscreenCanvas(width,height).getContext('2d');
  const frame=()=>{
    const output=host.ui_shader_frame(1,resource,width,height);
    gpu.begin(surface,[.98,.98,.97]);
    gpu.triangles(surface,new Float32Array(quad),resources.get(output),{x:0,y:0,width,height});
    gpu.present(surface,width,height,1);context.drawImage(canvas,0,0);
    return context.getImageData(0,0,width,height).data;
  };
  try {
    for(const input of [...data.bakes,...data.inputs]) uniform(input);
    for(const [pass,instances,draws] of [[data.shadowPass,data.shadowInstances,data.shadowDraws],[data.surfacePass,data.instances,data.draws]]) {
      for(const [slot,name] of ['vertices','colors'].entries()) words(data[name],(pointer,count)=>host.ui_shader_vertex_buffer(1,resource,pass,slot,pointer,count*4));
      words(instances,(pointer,count)=>host.ui_shader_vertex_buffer(1,resource,pass,2,pointer,count*4));
      words(data.indices,(pointer,count)=>host.ui_shader_index_buffer(1,resource,pass,pointer,count));
      words(draws,(pointer,count)=>host.ui_shader_draws(1,resource,pass,pointer,count/7));
    }
    const pixels=frame();
    for(let pass=0;pass<=data.shadowPass;pass++) host.ui_shader_pass_enabled(1,resource,pass,0);
    const retained=frame();
    if(pixels.some((value,index)=>value!==retained[index])) throw new Error(`${backend} Farm cached lighting or shadows changed the image`);
    const colors=new Set();let painted=0;
    for(let index=0;index<pixels.length;index+=4) {
      const [red,green,blue,alpha]=pixels.subarray(index,index+4);
      if(alpha!==255) throw new Error(`${backend} Farm left an uncomposited pixel`);
      colors.add(`${red>>3},${green>>3},${blue>>3}`);
      if(Math.max(red,green,blue)-Math.min(red,green,blue)>25) painted++;
    }
    if(colors.size<200||painted<width*height*.15) throw new Error(`${backend} Farm scene omitted its landscape: ${colors.size} colors, ${painted} painted pixels`);
    if(errors.length) throw errors[0];
    if(surface.gl&&surface.gl.getError()!==surface.gl.NO_ERROR) throw new Error('Farm scene produced a WebGL API error');
    return {pixels:[...pixels],colors:colors.size};
  } finally {services.closeSurface(surface);gpu.releaseSurface(surface);gpu.close();}
}

export function compareFarmScenes(first,second) {
  let total=0,different=0;
  for(let index=0;index<first.pixels.length;index+=4) {
    let maximum=0;
    for(let channel=0;channel<3;channel++) {
      const error=Math.abs(first.pixels[index+channel]-second.pixels[index+channel]);
      total+=error;maximum=Math.max(maximum,error);
    }
    if(maximum>12) different++;
  }
  const mean=total/(first.pixels.length*.75),fraction=different/(first.pixels.length/4);
  if(mean>2.5||fraction>.02) throw new Error(`Farm GPU backends disagree: mean channel error ${mean}, differing pixels ${fraction}`);
  return {mean,fraction,colors:[first.colors,second.colors]};
}
