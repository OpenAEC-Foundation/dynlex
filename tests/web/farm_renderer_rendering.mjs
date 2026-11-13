import {createGpu} from '/packages/dynlex-ui/platform/browser/gpu.js';
import {renderFarmScene,compareFarmScenes} from './scene-rendering.mjs';

const image=(name,format,size,dimension='2d',levels=1)=>({kind:'texture',name,format,dimension,layers:dimension==='cube'?6:1,levels,samples:1,size});
const attachment=(name,level=0,layer=0,load='load')=>({view:{image:name,level,layer},resolve:null,load,store:'store',clear:[0,0,0,1]});
const draw={count:3,instances:1,first:0,vertexOffset:0,instanceOffset:0,indexed:false,pipeline:0};
const encode=value=>Math.round(255*(value<=0.0031308?12.92*value:1.055*Math.pow(value,1/2.4)-0.055));

export async function verifyFarmRenderer() {
  const [stages,reference,queries,materials]=await Promise.all(['stages','sky-reference','lighting-data','material-reference'].map(name=>fetch(`/${name}.json`).then(response=>response.json())));
  const original=Object.getOwnPropertyDescriptor(Navigator.prototype,'gpu');
  try {
    await verify('webgpu',stages,reference.cases,queries,materials.cases);
    const first=await renderFarmScene('webgpu');
    Object.defineProperty(Navigator.prototype,'gpu',{configurable:true,get:()=>undefined});
    await verify('webgl',stages,reference.cases,queries,materials.cases);
    const second=await renderFarmScene('webgl');
    return {backends:['webgpu','webgl'],scene:compareFarmScenes(first,second)};
  } finally {Object.defineProperty(Navigator.prototype,'gpu',original);}
}

async function verify(backend,stages,reference,queries,materials) {
  const errors=[],gpu=await createGpu(error=>errors.push(error));
  if(gpu.backend!==backend) throw new Error(`Expected ${backend}; selected ${gpu.backend}`);
  const canvas=document.createElement('canvas');canvas.width=4;canvas.height=4;
  const surface=gpu.surface(canvas),context=new OffscreenCanvas(4,4).getContext('2d');
  const pass=(stage,color)=>({vertex:null,fragment:stage[backend],vertexBuffers:[],indices:null,draws:[draw],
    ...stage.reflection,pipelines:[{cullMode:'none',frontFace:'ccw',depthWrite:false,depthCompare:'always',blend:null}],color,depth:null,topology:'triangle-list'});
  const passes=[];
  for(let level=0;level<2;level++) for(let face=0;face<6;face++) passes.push(pass(stages.environment,attachment('reflection',level,face)));
  passes.push(pass(stages.query,attachment('output',0,0,'clear')));
  const descriptor={operation:'shader',output:'output',assets:[
    image('output','rgba8unorm-srgb',{kind:'frame'}),image('reflection','rgba16float',{kind:'fixed',width:8,height:8},'cube',2),
    {kind:'sampler',name:'pixel',minFilter:'nearest',magFilter:'nearest',mipmapFilter:'nearest',addressModeU:'clamp-to-edge',addressModeV:'clamp-to-edge',addressModeW:'clamp-to-edge',compare:'none',lodMinClamp:0,lodMaxClamp:32,maxAnisotropy:1}
  ],passes};
  const shader=await gpu.shader(surface,descriptor),quad=[];
  for(const [x,y] of [[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]]) quad.push(x*4,y*4,x,y,1,1,1,1);
  const uniform=(pass,binding,values,integer=false)=>shader.uniformBuffer(pass,0,binding,integer?1:0,1,values.length,
    new Uint8Array((integer?new Int32Array([...values,...Array(4-values.length).fill(0)]):new Float32Array([...values,...Array(4-values.length).fill(0)])).buffer));
  const query=(direction,color,level,label)=>{
    const scale=1/(1+Math.max(...color));
    uniform(12,0,[...direction,scale]);uniform(12,1,[level]);
    const texture=shader.render(4,4);
    gpu.begin(surface,[0,0,0]);gpu.triangles(surface,new Float32Array(quad),texture,{x:0,y:0,width:4,height:4});gpu.present(surface,4,4,1);
    context.drawImage(canvas,0,0);
    const actual=[...context.getImageData(2,2,1,1).data],expected=[...color.map(value=>encode(value*scale)),255];
    if(actual.some((value,channel)=>Math.abs(value-expected[channel])>4)) throw new Error(`${backend} Farm ${label}: ${actual}; expected ${expected}`);
  };
  try {
    uniform(12,4,[0,1,0,0]);uniform(12,5,[1,1,1,0]);
    for(let level=0;level<2;level++) for(let face=0;face<6;face++) {
      uniform(level*6+face,0,[level===0?8:4,level===0?8:4,face,level===0?0:0.65]);
      uniform(level*6+face,1,[128],true);
    }
    query(reference[0].direction,reference[0].color,-1,'initial sky');
    for(let pass=0;pass<12;pass++) shader.draws(pass,new Uint32Array());
    for(const [index,sample] of reference.entries()) query(sample.direction,sample.color,-1,`original sky ${index}`);
    for(const [index,sample] of queries.entries()) query(sample.direction,sample.color,sample.level,`reflection ${index}`);
    for(const [index,sample] of materials.entries()) {
      uniform(12,4,[...sample.normal,sample.time]);uniform(12,5,[...sample.base,0]);
      query(sample.point,sample.color.map(value=>value*0.5+0.5),-2-sample.mode,`material ${index}`);
    }
    if(errors.length) throw errors[0];
    if(surface.gl&&surface.gl.getError()!==surface.gl.NO_ERROR) throw new Error('Farm reflection produced a WebGL error');
  } finally {shader.close();gpu.releaseSurface(surface);gpu.close();}
}
