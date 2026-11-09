// Browser graphics capability selection shared by shader previews and DynLex UI.
// Select once, before binding a canvas to an API. Rendering errors after selection
// belong to the chosen renderer and must not trigger a different backend.
export async function requestGraphicsBackend() {
  if (!navigator.gpu) return { backend: "webgl" };
  try {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
    if (adapter === null) return { backend: "webgl" };
    const device = await adapter.requestDevice();
    return { backend: "webgpu", device };
  } catch (error) {
    console.warn("WebGPU device acquisition failed; selecting WebGL2.", error);
    return { backend: "webgl" };
  }
}

export function createWebGlProgram(gl, vertexSource, fragmentSource) {
  const shaders = [];
  let program = null;
  try {
    for (const [type, source] of [[gl.VERTEX_SHADER, vertexSource], [gl.FRAGMENT_SHADER, fragmentSource]]) {
      const shader = gl.createShader(type);
      if (!shader) throw new Error("WebGL could not allocate a shader");
      shaders.push(shader);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.error("WebGL shader compilation failed", gl.getShaderInfoLog(shader));
        throw new Error("An error occurred. Check the log.");
      }
    }
    program = gl.createProgram();
    if (!program) throw new Error("WebGL could not allocate a shader program");
    for (const shader of shaders) gl.attachShader(program, shader);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      console.error("WebGL shader linking failed", gl.getProgramInfoLog(program));
      throw new Error("An error occurred. Check the log.");
    }
    return program;
  } catch (error) {
    if (program) gl.deleteProgram(program);
    throw error;
  } finally {
    for (const shader of shaders) gl.deleteShader(shader);
  }
}
