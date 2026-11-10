// Test-only observation of the actual renderer's clipped text quads.
window.textProbe=[];
window.serviceProbe=[];
const instantiate=WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate=function(bytes,imports,...args) {
  const env=imports?.env;
  if(env?.ui_text_mask) {
    const begin=env.ui_begin,mask=env.ui_text_mask,triangles=env.ui_triangles;
    const captions=new Map(),decoder=new TextDecoder();
    const request=env.ui_request;
    env.ui_request=(surface,name,pointer,count)=>{
      const bytes=new Uint8Array(env.__linear_memory.buffer);let end=name;while(bytes[end]) end++;
      window.serviceProbe.push({service:decoder.decode(bytes.subarray(name,end)),payload:JSON.parse(decoder.decode(bytes.subarray(pointer,pointer+count)))});
      return request(surface,name,pointer,count);
    };
    env.ui_begin=(...args)=>{window.textProbe=[];return begin(...args);};
    env.ui_text_mask=(font,pointer,count,...args)=>{
      const result=mask(font,pointer,count,...args);
      if(result) captions.set(result,decoder.decode(new Uint8Array(env.__linear_memory.buffer,pointer,count)));
      return result;
    };
    env.ui_triangles=(surface,pointer,count,texture,x,y,width,height)=>{
      if(captions.has(texture)) {
        const vertices=new Float32Array(env.__linear_memory.buffer,pointer,count*8);
        let left=Infinity,top=Infinity,right=-Infinity,bottom=-Infinity;
        for(let i=0;i<vertices.length;i+=8) {
          left=Math.min(left,vertices[i]);right=Math.max(right,vertices[i]);
          top=Math.min(top,vertices[i+1]);bottom=Math.max(bottom,vertices[i+1]);
        }
        left=Math.max(left,x,0);top=Math.max(top,y,0);
        right=Math.min(right,x+width,innerWidth);bottom=Math.min(bottom,y+height,innerHeight);
        if(right>left&&bottom>top) window.textProbe.push({text:captions.get(texture),x:(left+right)/2,y:(top+bottom)/2});
      }
      return triangles(surface,pointer,count,texture,x,y,width,height);
    };
  }
  return instantiate(bytes,imports,...args);
};
