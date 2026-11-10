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
        const visible = vertices.some((value, index) => index % 8 === 0 &&
          value >= Math.max(x, 0) && value < Math.min(x + width, innerWidth) &&
          vertices[index + 1] >= Math.max(y, 0) && vertices[index + 1] < Math.min(y + height, innerHeight));
        if (visible) window.colorProbe.push({ text: captions.get(texture), ink: Array.from(vertices.slice(4, 7)) });
      }
      return triangles(surface, pointer, count, texture, x, y, width, height);
    };
  }
  return instantiateColors(bytes, imports, ...args);
};
