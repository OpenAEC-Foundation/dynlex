import {
  isGeneratedTerrainGeometryDescriptor,
  resolveTerrainGeometryDescriptor,
  validateTerrainGeometryDescriptor
} from "./terrain-geometry.js";

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
    deleteGeometryBinding(gl, pass);
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

export function validateShaderGeometryDescriptor(geometry, requireData = false) {
  const validInterface = (
    geometry
    && geometry.format === "float32x4"
    && typeof geometry.attributeEncoding === "string"
    && geometry.attributeEncoding.length > 0
    && geometry.primitive === "triangles"
    && typeof geometry.render?.backgroundPass === "boolean"
    && ["opaque", "additive"].includes(geometry.render.blendMode)
    && typeof geometry.render.depthTest === "boolean"
  );
  if (isGeneratedTerrainGeometryDescriptor(geometry)) {
    if (
      !validInterface
      || requireData
      || geometry.vertexCount !== undefined
      || geometry.data !== undefined
      || geometry.indices !== undefined
    ) {
      throw new Error("Invalid shader geometry");
    }
    validateTerrainGeometryDescriptor(geometry);
    return geometry;
  }

  const indices = geometry?.indices;
  const validIndices = indices === undefined || (
    indices
    && indices.format === "uint32"
    && Number.isInteger(indices.count)
    && indices.count > 0
    && (!requireData || indices.data instanceof ArrayBuffer)
    && (
      !(indices.data instanceof ArrayBuffer)
      || indices.data.byteLength === indices.count * Uint32Array.BYTES_PER_ELEMENT
    )
  );
  if (
    !validInterface
    || !Number.isInteger(geometry.vertexCount)
    || geometry.vertexCount <= 0
    || typeof geometry.render?.backgroundPass !== "boolean"
    || !["opaque", "additive"].includes(geometry.render.blendMode)
    || typeof geometry.render.depthTest !== "boolean"
    || (requireData && !(geometry.data instanceof ArrayBuffer))
    || (
      geometry.data instanceof ArrayBuffer
      && geometry.data.byteLength !== geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT
    )
    || !validIndices
  ) {
    throw new Error("Invalid shader geometry");
  }
  return geometry;
}

function createGeometryBinding(gl, geometry) {
  let vertexArray = null;
  let vertexBuffer = null;
  let indexBuffer = null;
  try {
    vertexArray = gl.createVertexArray();
    if (!vertexArray) {
      throw new Error("WebGL could not allocate a vertex array");
    }
    gl.bindVertexArray(vertexArray);
    if (geometry) {
      validateShaderGeometryDescriptor(geometry, true);
      vertexBuffer = gl.createBuffer();
      if (!vertexBuffer) {
        throw new Error("WebGL could not allocate a geometry buffer");
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 4 * Float32Array.BYTES_PER_ELEMENT, 0);
      if (geometry.indices) {
        indexBuffer = gl.createBuffer();
        if (!indexBuffer) {
          throw new Error("WebGL could not allocate an index buffer");
        }
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices.data, gl.STATIC_DRAW);
      }
    }
    return {
      vertexArray,
      vertexBuffer,
      indexBuffer,
      vertexCount: geometry ? geometry.vertexCount : 3,
      indexCount: geometry?.indices?.count ?? 0,
      indexType: geometry?.indices ? gl.UNSIGNED_INT : null
    };
  } catch (error) {
    if (indexBuffer) gl.deleteBuffer(indexBuffer);
    if (vertexBuffer) gl.deleteBuffer(vertexBuffer);
    if (vertexArray) gl.deleteVertexArray(vertexArray);
    throw error;
  }
}

function deleteGeometryBinding(gl, binding) {
  if (binding.indexBuffer) gl.deleteBuffer(binding.indexBuffer);
  if (binding.vertexBuffer) gl.deleteBuffer(binding.vertexBuffer);
  gl.deleteVertexArray(binding.vertexArray);
}

function replacePassGeometry(gl, pass, geometry) {
  const nextBinding = createGeometryBinding(gl, geometry);
  deleteGeometryBinding(gl, pass);
  Object.assign(pass, nextBinding);
}

function createProgramPass(
  gl,
  vertexSourceText,
  fragmentSourceText,
  reflectedUniforms,
  geometry,
  render,
  renderPass
) {
  let vertexShader = null;
  let fragmentShader = null;
  let program = null;
  let geometryBinding = null;
  let uniforms = [];
  try {
    vertexShader = compileShader(gl, gl.VERTEX_SHADER, vertexSourceText);
    fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, fragmentSourceText);
    program = linkProgram(gl, vertexShader, fragmentShader);
    geometryBinding = createGeometryBinding(gl, geometry);
    uniforms = createUniformBindings(gl, program, reflectedUniforms);
    return {
      program,
      uniforms,
      ...geometryBinding,
      render,
      renderPass,
      depthTest: render.depthTest
    };
  } catch (error) {
    for (const uniform of uniforms) gl.deleteBuffer(uniform.buffer);
    if (geometryBinding) deleteGeometryBinding(gl, geometryBinding);
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
    depth: true,
    powerPreference: "high-performance"
  });
  if (!gl) {
    throw new Error("This browser does not provide WebGL2");
  }

  const createdAt = performance.now();
  const elapsedSeconds = typeof options.elapsedSeconds === "function"
    ? options.elapsedSeconds
    : (timestamp) => (timestamp - createdAt) / 1000;
  if (
    options.geometryHorizontalPixels !== undefined
    && typeof options.geometryHorizontalPixels !== "function"
  ) {
    throw new Error("Shader preview geometry width provider must be a function");
  }
  let activeState = null;
  let revision = 0;
  let running = options.running !== false;
  let frameRequest = 0;
  let renderedFrameResolvers = [];

  function currentGeometryHorizontalPixels() {
    const horizontalPixels = options.geometryHorizontalPixels
      ? options.geometryHorizontalPixels()
      : gl.drawingBufferWidth;
    if (!Number.isSafeInteger(horizontalPixels) || horizontalPixels <= 0) {
      throw new Error("Shader preview geometry width must be a positive integer");
    }
    return horizontalPixels;
  }

  function updateGeometryDataset(geometry, horizontalPixels = null) {
    canvas.dataset.previewGeometryVertices = String(geometry?.vertexCount ?? 0);
    if (horizontalPixels === null) {
      delete canvas.dataset.previewGeometryHorizontalPixels;
    } else {
      canvas.dataset.previewGeometryHorizontalPixels = String(horizontalPixels);
    }
  }

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
    const geometrySource = programDescriptor.geometry ?? null;
    if (geometrySource) {
      validateShaderGeometryDescriptor(
        geometrySource,
        !isGeneratedTerrainGeometryDescriptor(geometrySource)
      );
    }
    resizeDrawingBuffer();
    const geometryHorizontalPixels = isGeneratedTerrainGeometryDescriptor(geometrySource)
      ? currentGeometryHorizontalPixels()
      : null;
    const geometry = geometryHorizontalPixels === null
      ? geometrySource
      : resolveTerrainGeometryDescriptor(geometrySource, geometryHorizontalPixels);

    const passes = [];
    let geometryPass = null;
    try {
      if (!geometry || geometry.render.backgroundPass) {
        passes.push(createProgramPass(
          gl,
          vertexSource,
          programDescriptor.fragmentSource,
          reflectedUniforms,
          null,
          { blendMode: "opaque", depthTest: false },
          0
        ));
      }
      if (geometry) {
        geometryPass = createProgramPass(
          gl,
          programDescriptor.vertexSource,
          programDescriptor.fragmentSource,
          reflectedUniforms,
          geometry,
          geometry.render,
          geometry.render.backgroundPass ? 1 : 0
        );
        passes.push(geometryPass);
      }
    } catch (error) {
      deleteProgramState(gl, { passes });
      throw error;
    }

    const previousState = activeState;
    activeState = {
      passes,
      geometrySource,
      geometryPass,
      geometryHorizontalPixels
    };
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    updateGeometryDataset(geometry, geometryHorizontalPixels);
    canvas.dataset.previewState = "ready";
    deleteProgramState(gl, previousState);
    if (!frameRequest) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
  }

  function resizeGeneratedGeometry() {
    if (!isGeneratedTerrainGeometryDescriptor(activeState?.geometrySource)) {
      return;
    }
    const horizontalPixels = currentGeometryHorizontalPixels();
    if (horizontalPixels === activeState.geometryHorizontalPixels) {
      return;
    }
    const geometry = resolveTerrainGeometryDescriptor(
      activeState.geometrySource,
      horizontalPixels
    );
    replacePassGeometry(gl, activeState.geometryPass, geometry);
    activeState.geometryHorizontalPixels = horizontalPixels;
    updateGeometryDataset(geometry, horizontalPixels);
  }

  function drawPass(pass, frame) {
    gl.useProgram(pass.program);
    gl.bindVertexArray(pass.vertexArray);
    for (const uniform of pass.uniforms) {
      const value = uniformValueProviders[uniform.name](frame, pass.renderPass);
      gl.bindBuffer(gl.UNIFORM_BUFFER, uniform.buffer);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, new Float32Array([value, 0, 0, 0]));
      gl.bindBufferBase(gl.UNIFORM_BUFFER, uniform.binding, uniform.buffer);
    }
    if (pass.indexCount > 0) {
      gl.drawElements(gl.TRIANGLES, pass.indexCount, pass.indexType, 0);
    } else {
      gl.drawArrays(gl.TRIANGLES, 0, pass.vertexCount);
    }
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
      resizeGeneratedGeometry();
      const frame = {
        elapsedSeconds: elapsedSeconds(timestamp),
        width: gl.drawingBufferWidth,
        height: gl.drawingBufferHeight
      };
      gl.viewport(0, 0, frame.width, frame.height);
      gl.clearColor(0, 0, 0, 1);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      for (const pass of activeState.passes) {
        if (pass.depthTest) {
          gl.enable(gl.DEPTH_TEST);
        } else {
          gl.disable(gl.DEPTH_TEST);
        }
        gl.depthMask(pass.depthTest);
        if (pass.render.blendMode === "additive") {
          gl.enable(gl.BLEND);
          gl.blendFunc(gl.ONE, gl.ONE);
        } else {
          gl.disable(gl.BLEND);
        }
        drawPass(pass, frame);
      }
      gl.depthMask(true);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
      const resolvers = renderedFrameResolvers;
      renderedFrameResolvers = [];
      for (const resolve of resolvers) resolve();
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

  function whenNextFrameRendered() {
    if (!activeState) {
      throw new Error("Shader preview cannot render before a program is installed");
    }
    const rendered = new Promise((resolve) => {
      renderedFrameResolvers.push(resolve);
    });
    if (!frameRequest) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
    return rendered;
  }

  canvas.dataset.previewState = "waiting";
  if (running) {
    frameRequest = requestAnimationFrame(drawFrame);
  }

  return Object.freeze({ replaceProgram, setRunning, whenNextFrameRendered });
}
