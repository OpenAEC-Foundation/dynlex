import {
  isGeneratedTerrainGeometryDescriptor,
  resolveTerrainGeometryDescriptor
} from "./terrain-geometry.js";
import { validateShaderGeometryDescriptor } from "./shader-renderer-shared.js";
import { createCloudTriangleVertices } from "./shader-transition-compositor.js";

const fullscreenVertexSource = `#version 300 es
precision highp float;
const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 uv;
void main() {
  vec2 position = positions[gl_VertexID];
  uv = position * 0.5 + 0.5;
  gl_Position = vec4(position, 0.0, 1.0);
}
`;

const copyFragmentSource = `#version 300 es
precision highp float;
uniform sampler2D frozenFrame;
in vec2 uv;
out vec4 color;
void main() { color = texture(frozenFrame, uv); }
`;

const cloudVertexSource = `#version 300 es
precision highp float;
layout(location = 0) in vec2 point;
uniform vec4 cloud;
void main() {
  vec2 pixel = cloud.xy + point * cloud.zw;
  gl_Position = vec4(pixel.x * 2.0 - 1.0, 1.0 - pixel.y * 2.0, 0.0, 1.0);
}
`;

const maskFragmentSource = `#version 300 es
precision highp float;
out vec4 color;
void main() { color = vec4(0.0); }
`;

const glowFragmentSource = `#version 300 es
precision highp float;
out vec4 color;
void main() { color = vec4(0.41, 0.84, 1.0, 0.24); }
`;

const uniformValueProviders = Object.freeze({
  time: (frame) => frame.elapsedSeconds,
  width: (frame) => frame.width,
  height: (frame) => frame.height,
  render_pass: (_frame, renderPass) => renderPass
});

function compileShader(gl, type, source) {
  const shader = gl.createShader(type);
  if (!shader) throw new Error("WebGL could not allocate a shader");
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) return shader;
  console.error("WebGL shader compilation failed", gl.getShaderInfoLog(shader));
  gl.deleteShader(shader);
  throw new Error("Shader preview could not compile. Check the browser log.");
}

function createProgram(gl, vertexSource, fragmentSource) {
  const vertex = compileShader(gl, gl.VERTEX_SHADER, vertexSource);
  const fragment = compileShader(gl, gl.FRAGMENT_SHADER, fragmentSource);
  const program = gl.createProgram();
  if (!program) {
    gl.deleteShader(vertex);
    gl.deleteShader(fragment);
    throw new Error("WebGL could not allocate a shader program");
  }
  gl.attachShader(program, vertex);
  gl.attachShader(program, fragment);
  gl.linkProgram(program);
  gl.deleteShader(vertex);
  gl.deleteShader(fragment);
  if (gl.getProgramParameter(program, gl.LINK_STATUS)) return program;
  console.error("WebGL shader linking failed", gl.getProgramInfoLog(program));
  gl.deleteProgram(program);
  throw new Error("Shader preview could not link. Check the browser log.");
}

function createGeometryBinding(gl, geometry) {
  const vertexArray = gl.createVertexArray();
  if (!vertexArray) throw new Error("WebGL could not allocate a vertex array");
  let vertexBuffer = null;
  let indexBuffer = null;
  try {
    gl.bindVertexArray(vertexArray);
    if (geometry) {
      validateShaderGeometryDescriptor(geometry, true);
      vertexBuffer = gl.createBuffer();
      if (!vertexBuffer) throw new Error("WebGL could not allocate a geometry buffer");
      gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.data, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 16, 0);
      if (geometry.indices) {
        indexBuffer = gl.createBuffer();
        if (!indexBuffer) throw new Error("WebGL could not allocate an index buffer");
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices.data, gl.STATIC_DRAW);
      }
    }
    return {
      vertexArray,
      vertexBuffer,
      indexBuffer,
      vertexCount: geometry ? geometry.vertexCount : 3,
      indexCount: geometry?.indices?.count ?? 0
    };
  } catch (error) {
    if (indexBuffer) gl.deleteBuffer(indexBuffer);
    if (vertexBuffer) gl.deleteBuffer(vertexBuffer);
    gl.deleteVertexArray(vertexArray);
    throw error;
  }
}

function destroyGeometryBinding(gl, binding) {
  if (binding.indexBuffer) gl.deleteBuffer(binding.indexBuffer);
  if (binding.vertexBuffer) gl.deleteBuffer(binding.vertexBuffer);
  gl.deleteVertexArray(binding.vertexArray);
}

function replaceGeometryBinding(gl, pass, geometry) {
  const replacement = createGeometryBinding(gl, geometry);
  destroyGeometryBinding(gl, pass);
  Object.assign(pass, replacement);
}

function createUniformBindings(gl, program, uniforms) {
  const activeUniformCount = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
  const active = [];
  for (let index = 0; index < activeUniformCount; index += 1) {
    const info = gl.getActiveUniform(program, index);
    if (info) active.push({ index, name: info.name });
  }
  const bindings = [];
  try {
    for (const uniform of uniforms) {
      const marker = `_group_${uniform.group}_binding_${uniform.binding}_`;
      const matches = active.filter(({ name }) => name.includes(marker));
      if (matches.length === 0) {
        throw new Error(`Compiled shader is missing the '${uniform.name}' uniform`);
      }
      const buffer = gl.createBuffer();
      if (!buffer) throw new Error("WebGL could not allocate a uniform buffer");
      gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
      gl.bufferData(gl.UNIFORM_BUFFER, 16, gl.DYNAMIC_DRAW);
      for (const match of matches) {
        const blockIndex = gl.getActiveUniforms(program, [match.index], gl.UNIFORM_BLOCK_INDEX)[0];
        gl.uniformBlockBinding(program, blockIndex, uniform.binding);
      }
      bindings.push({ ...uniform, buffer });
    }
    return bindings;
  } catch (error) {
    for (const binding of bindings) gl.deleteBuffer(binding.buffer);
    throw error;
  }
}

function mergeUniforms(...stages) {
  const byBinding = new Map();
  for (const uniforms of stages) {
    for (const uniform of uniforms) {
      const binding = `${uniform.group}:${uniform.binding}`;
      const current = byBinding.get(binding);
      if (current && current.name !== uniform.name) {
        throw new Error("Shader stages expose conflicting uniform bindings");
      }
      byBinding.set(binding, uniform);
    }
  }
  return [...byBinding.values()];
}

function createPass(gl, vertexSource, fragmentSource, uniforms, geometry, render, renderPass) {
  const program = createProgram(gl, vertexSource, fragmentSource);
  let binding;
  try {
    binding = createGeometryBinding(gl, geometry);
    return {
      program,
      uniforms: createUniformBindings(gl, program, uniforms),
      render,
      renderPass,
      ...binding
    };
  } catch (error) {
    if (binding) destroyGeometryBinding(gl, binding);
    gl.deleteProgram(program);
    throw error;
  }
}

function destroyState(gl, state) {
  if (!state) return;
  for (const pass of state.passes) {
    for (const uniform of pass.uniforms) gl.deleteBuffer(uniform.buffer);
    destroyGeometryBinding(gl, pass);
    gl.deleteProgram(pass.program);
  }
}

function validateProgram(program) {
  if (
    !program
    || typeof program.fragmentSource !== "string"
    || !Array.isArray(program.fragmentUniforms)
    || Boolean(program.vertexSource) !== Boolean(program.geometry)
    || Boolean(program.vertexSource) !== Array.isArray(program.vertexUniforms)
  ) {
    throw new Error("Compiler returned an invalid shader program descriptor");
  }
  for (const uniforms of [program.fragmentUniforms, ...(program.vertexUniforms ? [program.vertexUniforms] : [])]) {
    for (const uniform of uniforms) {
      if (
        typeof uniform.name !== "string"
        || !Number.isInteger(uniform.group)
        || !Number.isInteger(uniform.binding)
        || !(uniform.name in uniformValueProviders)
      ) {
        throw new Error("Compiler returned invalid shader-uniform reflection");
      }
    }
  }
}

function createTransition(gl) {
  const copyProgram = createProgram(gl, fullscreenVertexSource, copyFragmentSource);
  const maskProgram = createProgram(gl, cloudVertexSource, maskFragmentSource);
  const glowProgram = createProgram(gl, cloudVertexSource, glowFragmentSource);
  const cloudData = createCloudTriangleVertices();
  const cloudArray = gl.createVertexArray();
  const cloudBuffer = gl.createBuffer();
  if (!cloudArray || !cloudBuffer) throw new Error("WebGL could not allocate transition geometry");
  gl.bindVertexArray(cloudArray);
  gl.bindBuffer(gl.ARRAY_BUFFER, cloudBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, cloudData, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 8, 0);
  const emptyArray = gl.createVertexArray();
  if (!emptyArray) throw new Error("WebGL could not allocate a fullscreen vertex array");
  const copyTextureLocation = gl.getUniformLocation(copyProgram, "frozenFrame");
  const maskBoundsLocation = gl.getUniformLocation(maskProgram, "cloud");
  const glowBoundsLocation = gl.getUniformLocation(glowProgram, "cloud");
  if (copyTextureLocation === null || maskBoundsLocation === null || glowBoundsLocation === null) {
    throw new Error("WebGL transition uniforms are unavailable");
  }
  let texture = null;
  let framebuffer = null;
  let depthStencil = null;
  let width = 0;
  let height = 0;

  function resize(nextWidth, nextHeight) {
    if (width === nextWidth && height === nextHeight) return false;
    if (texture) gl.deleteTexture(texture);
    if (framebuffer) gl.deleteFramebuffer(framebuffer);
    if (depthStencil) gl.deleteRenderbuffer(depthStencil);
    texture = gl.createTexture();
    framebuffer = gl.createFramebuffer();
    depthStencil = gl.createRenderbuffer();
    if (!texture || !framebuffer || !depthStencil) {
      throw new Error("WebGL could not allocate transition attachments");
    }
    width = nextWidth;
    height = nextHeight;
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.bindRenderbuffer(gl.RENDERBUFFER, depthStencil);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH24_STENCIL8, width, height);
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
    gl.framebufferRenderbuffer(
      gl.FRAMEBUFFER,
      gl.DEPTH_STENCIL_ATTACHMENT,
      gl.RENDERBUFFER,
      depthStencil
    );
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new Error("WebGL transition framebuffer is incomplete");
    }
    return true;
  }

  function normalizedBounds(bounds, expansion = 0) {
    return [
      (bounds.left - expansion) / bounds.frameWidth,
      (bounds.top - expansion) / bounds.frameHeight,
      (bounds.width + expansion * 2) / bounds.frameWidth,
      (bounds.height + expansion * 2) / bounds.frameHeight
    ];
  }

  function draw(bounds, drawIncoming) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, width, height);
    gl.colorMask(true, true, true, true);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.STENCIL_TEST);
    gl.disable(gl.BLEND);
    gl.useProgram(copyProgram);
    gl.bindVertexArray(emptyArray);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.uniform1i(copyTextureLocation, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
    gl.useProgram(glowProgram);
    gl.uniform4fv(glowBoundsLocation, normalizedBounds(bounds, 18));
    gl.bindVertexArray(cloudArray);
    gl.drawArrays(gl.TRIANGLES, 0, cloudData.length / 2);
    gl.disable(gl.BLEND);

    gl.enable(gl.STENCIL_TEST);
    gl.stencilMask(0xff);
    gl.stencilFunc(gl.ALWAYS, 1, 0xff);
    gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
    gl.colorMask(false, false, false, false);
    gl.useProgram(maskProgram);
    gl.uniform4fv(maskBoundsLocation, normalizedBounds(bounds));
    gl.drawArrays(gl.TRIANGLES, 0, cloudData.length / 2);
    gl.colorMask(true, true, true, true);
    gl.stencilMask(0);
    gl.stencilFunc(gl.EQUAL, 1, 0xff);
    drawIncoming();
    gl.disable(gl.STENCIL_TEST);
    gl.stencilMask(0xff);
  }

  function destroy() {
    if (texture) gl.deleteTexture(texture);
    if (framebuffer) gl.deleteFramebuffer(framebuffer);
    if (depthStencil) gl.deleteRenderbuffer(depthStencil);
    gl.deleteBuffer(cloudBuffer);
    gl.deleteVertexArray(cloudArray);
    gl.deleteVertexArray(emptyArray);
    gl.deleteProgram(copyProgram);
    gl.deleteProgram(maskProgram);
    gl.deleteProgram(glowProgram);
  }

  return { resize, framebuffer: () => framebuffer, draw, destroy };
}

export function createWebGlShaderPreview(canvas, options = {}) {
  if (!(canvas instanceof HTMLCanvasElement)) throw new Error("Shader preview requires a canvas");
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    antialias: true,
    depth: true,
    stencil: true,
    powerPreference: "high-performance"
  });
  if (!gl) throw new Error("This browser does not provide WebGL2");
  const createdAt = performance.now();
  const elapsedSeconds = typeof options.elapsedSeconds === "function"
    ? options.elapsedSeconds
    : (timestamp) => (timestamp - createdAt) / 1000;
  let activeState = null;
  let preparedState = null;
  let transition = null;
  let bounds = null;
  let snapshotRequired = false;
  let revision = 0;
  let running = options.running !== false;
  let frameRequest = 0;
  let destroyed = false;
  let contextUnavailable = false;
  let frameResolvers = [];

  function requireAvailable() {
    if (destroyed) throw new Error("Shader preview has been destroyed");
    if (contextUnavailable) {
      throw new Error("Shader preview is unavailable because its WebGL context was lost");
    }
  }

  function rejectFrameWaiters(error) {
    const waiters = frameResolvers;
    frameResolvers = [];
    for (const waiter of waiters) waiter.reject(error);
  }

  const contextLost = () => {
    if (destroyed) return;
    contextUnavailable = true;
    running = false;
    cancelAnimationFrame(frameRequest);
    frameRequest = 0;
    canvas.dataset.previewState = "lost";
    rejectFrameWaiters(new Error("Shader preview could not render. Check the browser log."));
  };
  canvas.addEventListener("webglcontextlost", contextLost);

  function scheduleFrame() {
    requireAvailable();
    if (!frameRequest) frameRequest = requestAnimationFrame(drawFrame);
  }

  function resizeDrawingBuffer() {
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.ceil(canvas.clientWidth * ratio));
    const height = Math.max(1, Math.ceil(canvas.clientHeight * ratio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
  }

  function horizontalPixels() {
    const value = options.geometryHorizontalPixels
      ? options.geometryHorizontalPixels()
      : gl.drawingBufferWidth;
    if (!Number.isSafeInteger(value) || value <= 0) {
      throw new Error("Shader preview geometry width must be a positive integer");
    }
    return value;
  }

  function updateDataset(state, prepared) {
    const prefix = prepared ? "previewPrepared" : "preview";
    canvas.dataset[`${prefix}GeometryVertices`] = String(state.geometryPass?.vertexCount ?? 0);
    if (state.geometryHorizontalPixels === null) {
      delete canvas.dataset[`${prefix}GeometryHorizontalPixels`];
    } else {
      canvas.dataset[`${prefix}GeometryHorizontalPixels`] = String(state.geometryHorizontalPixels);
    }
  }

  function compileProgram(program) {
    validateProgram(program);
    resizeDrawingBuffer();
    const geometrySource = program.geometry ?? null;
    if (geometrySource) {
      validateShaderGeometryDescriptor(
        geometrySource,
        !isGeneratedTerrainGeometryDescriptor(geometrySource)
      );
    }
    const geometryHorizontalPixels = isGeneratedTerrainGeometryDescriptor(geometrySource)
      ? horizontalPixels()
      : null;
    const geometry = geometryHorizontalPixels === null
      ? geometrySource
      : resolveTerrainGeometryDescriptor(geometrySource, geometryHorizontalPixels);
    const passes = [];
    try {
      if (!geometry || geometry.render.backgroundPass) {
        passes.push(createPass(
          gl,
          fullscreenVertexSource,
          program.fragmentSource,
          program.fragmentUniforms,
          null,
          { blendMode: "opaque", depthTest: false },
          0
        ));
      }
      if (geometry) {
        passes.push(createPass(
          gl,
          program.vertexSource,
          program.fragmentSource,
          mergeUniforms(program.fragmentUniforms, program.vertexUniforms),
          geometry,
          geometry.render,
          geometry.render.backgroundPass ? 1 : 0
        ));
      }
    } catch (error) {
      destroyState(gl, { passes });
      throw error;
    }
    return {
      passes,
      geometrySource,
      geometryPass: geometry ? passes.at(-1) : null,
      geometryHorizontalPixels
    };
  }

  function replaceProgram(program) {
    requireAvailable();
    const state = compileProgram(program);
    destroyState(gl, activeState);
    destroyState(gl, preparedState);
    activeState = state;
    preparedState = null;
    bounds = null;
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    updateDataset(state, false);
    canvas.dataset.previewState = "ready";
    scheduleFrame();
    return Promise.resolve(true);
  }

  function prepareProgram(program) {
    requireAvailable();
    const state = compileProgram(program);
    destroyState(gl, preparedState);
    preparedState = state;
    transition ??= createTransition(gl);
    canvas.dataset.previewPreparedRevision = String(revision + 1);
    updateDataset(state, true);
    return Promise.resolve(true);
  }

  function resizeGeometry(state, prepared) {
    if (!isGeneratedTerrainGeometryDescriptor(state?.geometrySource)) return;
    const width = horizontalPixels();
    if (width === state.geometryHorizontalPixels) return;
    const geometry = resolveTerrainGeometryDescriptor(state.geometrySource, width);
    replaceGeometryBinding(gl, state.geometryPass, geometry);
    state.geometryHorizontalPixels = width;
    updateDataset(state, prepared);
  }

  function drawState(state, frame) {
    for (const pass of state.passes) {
      gl.useProgram(pass.program);
      gl.bindVertexArray(pass.vertexArray);
      for (const uniform of pass.uniforms) {
        const value = uniformValueProviders[uniform.name](frame, pass.renderPass);
        gl.bindBuffer(gl.UNIFORM_BUFFER, uniform.buffer);
        gl.bufferSubData(gl.UNIFORM_BUFFER, 0, new Float32Array([value, 0, 0, 0]));
        gl.bindBufferBase(gl.UNIFORM_BUFFER, uniform.binding, uniform.buffer);
      }
      if (pass.render.depthTest) gl.enable(gl.DEPTH_TEST);
      else gl.disable(gl.DEPTH_TEST);
      gl.depthMask(pass.render.depthTest);
      if (pass.render.blendMode === "additive") {
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE);
      } else {
        gl.disable(gl.BLEND);
      }
      if (pass.indexCount) gl.drawElements(gl.TRIANGLES, pass.indexCount, gl.UNSIGNED_INT, 0);
      else gl.drawArrays(gl.TRIANGLES, 0, pass.vertexCount);
    }
  }

  function clearFrame() {
    gl.colorMask(true, true, true, true);
    gl.depthMask(true);
    gl.stencilMask(0xff);
    gl.clearColor(0, 0, 0, 1);
    gl.clearDepth(1);
    gl.clearStencil(0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
  }

  function drawFrame(timestamp) {
    frameRequest = 0;
    if (destroyed) return;
    resizeDrawingBuffer();
    if (activeState) {
      resizeGeometry(activeState, false);
      resizeGeometry(preparedState, true);
      const activeFrame = {
        elapsedSeconds: elapsedSeconds(timestamp, "active"),
        width: gl.drawingBufferWidth,
        height: gl.drawingBufferHeight
      };
      if (bounds) {
        if (!preparedState || !transition) throw new Error("WebGL transition is incomplete");
        if (transition.resize(gl.drawingBufferWidth, gl.drawingBufferHeight)) snapshotRequired = true;
        if (snapshotRequired) {
          gl.bindFramebuffer(gl.FRAMEBUFFER, transition.framebuffer());
          gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
          gl.disable(gl.STENCIL_TEST);
          clearFrame();
          drawState(activeState, activeFrame);
          snapshotRequired = false;
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        clearFrame();
        transition.draw(bounds, () => drawState(preparedState, {
          elapsedSeconds: elapsedSeconds(timestamp, "prepared"),
          width: gl.drawingBufferWidth,
          height: gl.drawingBufferHeight
        }));
      } else {
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
        gl.disable(gl.STENCIL_TEST);
        clearFrame();
        drawState(activeState, activeFrame);
      }
      canvas.dataset.previewElapsedSeconds = String(
        bounds ? elapsedSeconds(timestamp, "prepared") : activeFrame.elapsedSeconds
      );
      const resolvers = frameResolvers;
      frameResolvers = [];
      for (const resolver of resolvers) resolver.resolve();
    }
    if (running) scheduleFrame();
  }

  function beginTransition() {
    requireAvailable();
    if (!preparedState) throw new Error("Shader transition requires a prepared program");
    snapshotRequired = true;
    canvas.dataset.previewTransitionState = "active";
  }

  function setTransition(nextBounds) {
    requireAvailable();
    if (!preparedState) throw new Error("Shader transition requires a prepared program");
    if (
      !nextBounds
      || !["left", "top", "width", "height", "frameWidth", "frameHeight"].every(
        (name) => typeof nextBounds[name] === "number" && Number.isFinite(nextBounds[name])
      )
      || nextBounds.width <= 0
      || nextBounds.height <= 0
      || nextBounds.frameWidth <= 0
      || nextBounds.frameHeight <= 0
    ) {
      throw new Error("Shader transition bounds are invalid");
    }
    bounds = nextBounds;
    scheduleFrame();
  }

  function promotePrepared() {
    requireAvailable();
    if (!preparedState) throw new Error("Shader transition has no prepared program to promote");
    destroyState(gl, activeState);
    activeState = preparedState;
    preparedState = null;
    bounds = null;
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    updateDataset(activeState, false);
    for (const name of [
      "previewPreparedRevision",
      "previewPreparedGeometryVertices",
      "previewPreparedGeometryHorizontalPixels",
      "previewTransitionState"
    ]) delete canvas.dataset[name];
    scheduleFrame();
  }

  function discardPrepared() {
    requireAvailable();
    destroyState(gl, preparedState);
    preparedState = null;
    bounds = null;
    for (const name of [
      "previewPreparedRevision",
      "previewPreparedGeometryVertices",
      "previewPreparedGeometryHorizontalPixels",
      "previewTransitionState"
    ]) delete canvas.dataset[name];
  }

  function setRunning(nextRunning) {
    requireAvailable();
    running = Boolean(nextRunning);
    if (running) scheduleFrame();
  }

  function whenNextFrameRendered() {
    requireAvailable();
    if (!activeState) throw new Error("Shader preview cannot render before a program is installed");
    const promise = new Promise((resolve, reject) => frameResolvers.push({ resolve, reject }));
    scheduleFrame();
    return promise;
  }

  function destroy() {
    if (destroyed) return;
    destroyed = true;
    canvas.removeEventListener("webglcontextlost", contextLost);
    cancelAnimationFrame(frameRequest);
    destroyState(gl, activeState);
    destroyState(gl, preparedState);
    transition?.destroy();
    rejectFrameWaiters(new Error("Shader preview was destroyed before rendering completed"));
    canvas.dataset.previewState = "destroyed";
  }

  canvas.dataset.previewApi = "webgl2";
  canvas.dataset.previewState = "waiting";
  if (running) scheduleFrame();
  return Object.freeze({
    replaceProgram,
    prepareProgram,
    beginTransition,
    setTransition,
    promotePrepared,
    discardPrepared,
    setRunning,
    whenNextFrameRendered,
    destroy
  });
}
