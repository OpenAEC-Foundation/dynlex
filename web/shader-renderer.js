const vertexSource = `#version 300 es
precision highp float;

const vec2 positions[3] = vec2[3](
  vec2(-1.0, -1.0),
  vec2(3.0, -1.0),
  vec2(-1.0, 3.0)
);

void main() {
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
`;

const uniformValueProviders = Object.freeze({
  time(frame) {
    return frame.elapsedSeconds;
  },
  width(frame) {
    return frame.width;
  },
  height(frame) {
    return frame.height;
  },
  render_pass(_frame, renderPass) {
    return renderPass;
  }
});

function compileShader(gl, type, source) {
  const shader = gl.createShader(type);
  if (!shader) {
    throw new Error("WebGL could not allocate a shader");
  }
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    return shader;
  }
  console.error("WebGL shader compilation failed", gl.getShaderInfoLog(shader));
  gl.deleteShader(shader);
  throw new Error("Shader preview could not compile. Check the browser log.");
}

function linkProgram(gl, vertexShader, fragmentShader) {
  const program = gl.createProgram();
  if (!program) {
    throw new Error("WebGL could not allocate a shader program");
  }
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  if (gl.getProgramParameter(program, gl.LINK_STATUS)) {
    return program;
  }
  console.error("WebGL shader linking failed", gl.getProgramInfoLog(program));
  gl.deleteProgram(program);
  throw new Error("Shader preview could not link. Check the browser log.");
}

function validateUniform(uniform) {
  if (
    !uniform
    || typeof uniform.name !== "string"
    || typeof uniform.block !== "string"
    || !Number.isInteger(uniform.binding)
    || uniform.binding < 0
  ) {
    throw new Error("Compiler returned invalid shader-uniform reflection");
  }
  if (!(uniform.name in uniformValueProviders)) {
    throw new Error(`The browser preview does not provide the '${uniform.name}' shader uniform`);
  }
}

function deleteProgramState(gl, state) {
  if (!state) return;
  for (const pass of state.passes) {
    for (const uniform of pass.uniforms) {
      gl.deleteBuffer(uniform.buffer);
    }
    if (pass.vertexBuffer) gl.deleteBuffer(pass.vertexBuffer);
    gl.deleteVertexArray(pass.vertexArray);
    gl.deleteProgram(pass.program);
  }
}

function createUniformBindings(gl, program, reflectedUniforms) {
  const uniforms = [];
  try {
    for (const uniform of reflectedUniforms) {
      const blockIndex = gl.getUniformBlockIndex(program, uniform.block);
      if (blockIndex === gl.INVALID_INDEX) {
        throw new Error(`Compiled shader is missing uniform block '${uniform.block}'`);
      }
      const buffer = gl.createBuffer();
      if (!buffer) {
        throw new Error("WebGL could not allocate a uniform buffer");
      }
      gl.uniformBlockBinding(program, blockIndex, uniform.binding);
      gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
      gl.bufferData(gl.UNIFORM_BUFFER, 16, gl.DYNAMIC_DRAW);
      uniforms.push({ ...uniform, buffer });
    }
    return uniforms;
  } catch (error) {
    for (const uniform of uniforms) gl.deleteBuffer(uniform.buffer);
    throw error;
  }
}

function validateGeometry(geometry) {
  if (
    !geometry
    || geometry.format !== "float32x4"
    || geometry.primitive !== "triangles"
    || !Number.isInteger(geometry.pointCount)
    || geometry.pointCount <= 0
    || geometry.vertexCount !== geometry.pointCount * 3
    || !(geometry.data instanceof ArrayBuffer)
    || geometry.data.byteLength !== geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT
  ) {
    throw new Error("Invalid shader geometry");
  }
}

function createProgramPass(gl, vertexSourceText, fragmentSourceText, reflectedUniforms, geometry = null) {
  let vertexShader = null;
  let fragmentShader = null;
  let program = null;
  let vertexArray = null;
  let vertexBuffer = null;
  let uniforms = [];
  try {
    vertexShader = compileShader(gl, gl.VERTEX_SHADER, vertexSourceText);
    fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, fragmentSourceText);
    program = linkProgram(gl, vertexShader, fragmentShader);
    vertexArray = gl.createVertexArray();
    if (!vertexArray) {
      throw new Error("WebGL could not allocate a vertex array");
    }
    gl.bindVertexArray(vertexArray);
    if (geometry) {
      validateGeometry(geometry);
      vertexBuffer = gl.createBuffer();
      if (!vertexBuffer) {
        throw new Error("WebGL could not allocate a geometry buffer");
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 4 * Float32Array.BYTES_PER_ELEMENT, 0);
    }
    uniforms = createUniformBindings(gl, program, reflectedUniforms);
    return {
      program,
      uniforms,
      vertexArray,
      vertexBuffer,
      vertexCount: geometry ? geometry.vertexCount : 3
    };
  } catch (error) {
    for (const uniform of uniforms) gl.deleteBuffer(uniform.buffer);
    if (vertexBuffer) gl.deleteBuffer(vertexBuffer);
    if (vertexArray) gl.deleteVertexArray(vertexArray);
    if (program) gl.deleteProgram(program);
    throw error;
  } finally {
    if (vertexShader) gl.deleteShader(vertexShader);
    if (fragmentShader) gl.deleteShader(fragmentShader);
  }
}

export function createShaderPreview(canvas, options = {}) {
  if (!(canvas instanceof HTMLCanvasElement)) {
    throw new Error("Shader preview requires a canvas");
  }
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    antialias: true,
    depth: false,
    powerPreference: "high-performance"
  });
  if (!gl) {
    throw new Error("This browser does not provide WebGL2");
  }

  const createdAt = performance.now();
  const elapsedSeconds = typeof options.elapsedSeconds === "function"
    ? options.elapsedSeconds
    : (timestamp) => (timestamp - createdAt) / 1000;
  let activeState = null;
  let revision = 0;
  let running = options.running !== false;
  let frameRequest = 0;

  function replaceProgram(programDescriptor, reflectedUniforms) {
    if (
      !programDescriptor
      || typeof programDescriptor.fragmentSource !== "string"
      || programDescriptor.fragmentSource.length === 0
      || (programDescriptor.vertexSource !== undefined
        && (typeof programDescriptor.vertexSource !== "string" || programDescriptor.vertexSource.length === 0))
      || Boolean(programDescriptor.vertexSource) !== Boolean(programDescriptor.geometry)
    ) {
      throw new Error("Compiler returned an invalid shader program descriptor");
    }
    if (!Array.isArray(reflectedUniforms)) {
      throw new Error("Compiler returned invalid shader uniforms");
    }
    reflectedUniforms.forEach(validateUniform);

    const passes = [];
    try {
      passes.push(createProgramPass(gl, vertexSource, programDescriptor.fragmentSource, reflectedUniforms));
      if (programDescriptor.geometry) {
        passes.push(createProgramPass(
          gl,
          programDescriptor.vertexSource,
          programDescriptor.fragmentSource,
          reflectedUniforms,
          programDescriptor.geometry
        ));
      }
    } catch (error) {
      deleteProgramState(gl, { passes });
      throw error;
    }

    const previousState = activeState;
    activeState = { passes };
    resizeDrawingBuffer();
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    canvas.dataset.previewGeometryPoints = String(programDescriptor.geometry?.pointCount ?? 0);
    canvas.dataset.previewState = "ready";
    deleteProgramState(gl, previousState);
    if (!frameRequest) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
  }

  function drawPass(pass, frame, renderPass) {
    gl.useProgram(pass.program);
    gl.bindVertexArray(pass.vertexArray);
    for (const uniform of pass.uniforms) {
      const value = uniformValueProviders[uniform.name](frame, renderPass);
      gl.bindBuffer(gl.UNIFORM_BUFFER, uniform.buffer);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, new Float32Array([value, 0, 0, 0]));
      gl.bindBufferBase(gl.UNIFORM_BUFFER, uniform.binding, uniform.buffer);
    }
    gl.drawArrays(gl.TRIANGLES, 0, pass.vertexCount);
  }

  function resizeDrawingBuffer() {
    const pixelRatio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.ceil(canvas.clientWidth * pixelRatio));
    const height = Math.max(1, Math.ceil(canvas.clientHeight * pixelRatio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
  }

  function drawFrame(timestamp) {
    frameRequest = 0;
    resizeDrawingBuffer();
    if (activeState) {
      const frame = {
        elapsedSeconds: elapsedSeconds(timestamp),
        width: gl.drawingBufferWidth,
        height: gl.drawingBufferHeight
      };
      gl.viewport(0, 0, frame.width, frame.height);
      gl.disable(gl.BLEND);
      drawPass(activeState.passes[0], frame, 0);
      if (activeState.passes.length === 2) {
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE);
        drawPass(activeState.passes[1], frame, 1);
        gl.disable(gl.BLEND);
      }
    }
    if (running) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
  }

  function setRunning(nextRunning) {
    running = Boolean(nextRunning);
    if (running && !frameRequest) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
  }

  canvas.dataset.previewState = "waiting";
  if (running) {
    frameRequest = requestAnimationFrame(drawFrame);
  }

  return Object.freeze({ replaceProgram, setRunning });
}
