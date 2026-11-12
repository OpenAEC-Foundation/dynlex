// Test-only observation of colored glyph quads on the actual shared renderer.
window.colorProbe = [];
const instantiateColors = WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate = function(bytes, imports, ...args) {
  const env = imports?.env;
  if (env?.ui_text_mask) {
    const begin = env.ui_begin, mask = env.ui_text_mask, triangles = env.ui_triangles;
    const captions = new Map(), decoder = new TextDecoder();
    env.ui_begin = (...values) => { window.colorProbe = []; return begin(...values); };
    env.ui_text_mask = (font, pointer, count, ...values) => {
      const result = mask(font, pointer, count, ...values);
      if (result) captions.set(result, decoder.decode(new Uint8Array(env.__linear_memory.buffer, pointer, count)));
      return result;
    };
    env.ui_triangles = (surface, pointer, count, texture, x, y, width, height) => {
      if (captions.has(texture) && count > 0) {
        const vertices = new Float32Array(env.__linear_memory.buffer, pointer, count * 8);
        let left=Infinity,top=Infinity,right=-Infinity,bottom=-Infinity;
        for(let index=0;index<vertices.length;index+=8) {
          left=Math.min(left,vertices[index]);right=Math.max(right,vertices[index]);
          top=Math.min(top,vertices[index+1]);bottom=Math.max(bottom,vertices[index+1]);
        }
        left=Math.max(left,x,0);top=Math.max(top,y,0);
        right=Math.min(right,x+width,innerWidth);bottom=Math.min(bottom,y+height,innerHeight);
        if(right>left && bottom>top) window.colorProbe.push({text:captions.get(texture),
          ink:Array.from(vertices.slice(4,7)),x:(left+right)/2,y:(top+bottom)/2,
          width:right-left,height:bottom-top});
      }
      return triangles(surface, pointer, count, texture, x, y, width, height);
    };
  }
  return instantiateColors(bytes, imports, ...args);
};
