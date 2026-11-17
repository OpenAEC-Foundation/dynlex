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

function startWebGlProgram(gl, vertexSource, fragmentSource) {
  const work = { shaders: [], program: null };
  try {
    for (const [type, source] of [[gl.VERTEX_SHADER, vertexSource], [gl.FRAGMENT_SHADER, fragmentSource]]) {
      const shader = gl.createShader(type);
      if (!shader) throw new Error("WebGL could not allocate a shader");
      work.shaders.push(shader);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
    }
    work.program = gl.createProgram();
    if (!work.program) throw new Error("WebGL could not allocate a shader program");
    for (const shader of work.shaders) gl.attachShader(work.program, shader);
    gl.linkProgram(work.program);
    return work;
  } catch (error) {
    discardWebGlProgram(gl, work);
    throw error;
  }
}

function discardWebGlProgram(gl, work) {
  if (work.program) gl.deleteProgram(work.program);
  for (const shader of work.shaders) gl.deleteShader(shader);
}

function finishWebGlProgram(gl, work) {
  try {
    for (const shader of work.shaders) {
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.error("WebGL shader compilation failed", gl.getShaderInfoLog(shader));
        throw new Error("An error occurred. Check the log.");
      }
    }
    if (!gl.getProgramParameter(work.program, gl.LINK_STATUS)) {
      console.error("WebGL shader linking failed", gl.getProgramInfoLog(work.program));
      throw new Error("An error occurred. Check the log.");
    }
    return work.program;
  } catch (error) {
    if (work.program) gl.deleteProgram(work.program);
    throw error;
  } finally {
    for (const shader of work.shaders) gl.deleteShader(shader);
  }
}

export function createWebGlProgram(gl, vertexSource, fragmentSource) {
  return finishWebGlProgram(gl, startWebGlProgram(gl, vertexSource, fragmentSource));
}

// Compile/link immediately, then use the driver's nonblocking completion query.
// Drivers without this extension expose only the standard blocking status query.
export async function createWebGlProgramAsync(gl, vertexSource, fragmentSource) {
  const extension = gl.getExtension('KHR_parallel_shader_compile');
  const work = startWebGlProgram(gl, vertexSource, fragmentSource);
  try {
    if (extension !== null) {
      while (!gl.getProgramParameter(work.program, extension.COMPLETION_STATUS_KHR))
        await new Promise(resolve => setTimeout(resolve, 0));
    } else {
      await new Promise(resolve => setTimeout(resolve, 0));
    }
  } catch (error) {
    discardWebGlProgram(gl, work);
    throw error;
  }
  return finishWebGlProgram(gl, work);
}
