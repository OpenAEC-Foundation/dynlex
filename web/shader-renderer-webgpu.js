import {
  isGeneratedTerrainGeometryDescriptor,
  resolveTerrainGeometryDescriptor
} from "./terrain-geometry.js";
import { validateShaderGeometryDescriptor } from "./shader-renderer-shared.js";
import { createTransitionCompositor } from "./shader-transition-compositor.js";

const fullscreenVertexSource = `
@vertex
fn main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
  let positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  return vec4<f32>(positions[vertexIndex], 0.0, 1.0);
}
`;

const depthFormat = "depth24plus-stencil8";
const maximumPendingSubmissions = 2;
let sharedDevicePromise = null;
let sharedDevice = null;
const deviceLossObservers = new Set();

async function createWebGpuDevice() {
  let adapter;
  try {
    adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
  } catch (error) {
    console.error("WebGPU adapter request failed", error);
    throw new Error("Shader preview could not initialize WebGPU. Check the browser log.");
  }
  if (!adapter) {
    throw new Error("This browser could not find a compatible WebGPU adapter");
  }

  let device;
  try {
    device = await adapter.requestDevice();
  } catch (error) {
    console.error("WebGPU device request failed", error);
    throw new Error("Shader preview could not initialize WebGPU. Check the browser log.");
  }
  sharedDevice = device;
  device.addEventListener("uncapturederror", (event) => {
    console.error("Uncaptured WebGPU device error", event.error.message, event.error);
  });
  device.lost.then((information) => {
    console.error("WebGPU device was lost", information);
    if (sharedDevice === device) {
      sharedDevice = null;
      sharedDevicePromise = null;
    }
    const observers = [...deviceLossObservers];
    deviceLossObservers.clear();
    for (const observer of observers) observer();
  });
  return device;
}

async function requestWebGpuDevice() {
  if (!navigator.gpu) {
    throw new Error("This browser does not provide WebGPU");
  }
  if (!sharedDevicePromise) {
    sharedDevicePromise = createWebGpuDevice();
  }
  try {
    return await sharedDevicePromise;
  } catch (error) {
    sharedDevice = null;
    sharedDevicePromise = null;
    throw error;
  }
}

export async function hasWebGpuDevice() {
  if (!navigator.gpu) return false;
  try {
    await requestWebGpuDevice();
    return true;
  } catch {
    return false;
  }
}

function observeWebGpuDeviceLoss(device, observer) {
  if (device !== sharedDevice) {
    throw new Error("Cannot observe a WebGPU device that is not active");
  }
  deviceLossObservers.add(observer);
  return () => deviceLossObservers.delete(observer);
}

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

function validateUniform(uniform) {
  if (
    !uniform
    || typeof uniform.name !== "string"
    || !Number.isInteger(uniform.group)
    || uniform.group < 0
    || !Number.isInteger(uniform.binding)
    || uniform.binding < 0
  ) {
    throw new Error("Compiler returned invalid shader-uniform reflection");
  }
  if (!(uniform.name in uniformValueProviders)) {
    throw new Error(`The browser preview does not provide the '${uniform.name}' shader uniform`);
  }
}

function validateStageUniforms(uniforms) {
  if (!Array.isArray(uniforms)) {
    throw new Error("Compiler returned invalid shader uniforms");
  }
  const bindings = new Set();
  const names = new Set();
  for (const uniform of uniforms) {
    validateUniform(uniform);
    const binding = `${uniform.group}:${uniform.binding}`;
    if (bindings.has(binding) || names.has(uniform.name)) {
      throw new Error("Compiler returned duplicate shader-uniform reflection");
    }
    bindings.add(binding);
    names.add(uniform.name);
  }
}

function mergePassUniforms(...stageUniforms) {
  const byBinding = new Map();
  const bindingByName = new Map();
  for (const uniforms of stageUniforms) {
    for (const uniform of uniforms) {
      const binding = `${uniform.group}:${uniform.binding}`;
      const existingAtBinding = byBinding.get(binding);
      const existingBinding = bindingByName.get(uniform.name);
      if (
        (existingAtBinding && existingAtBinding.name !== uniform.name)
        || (existingBinding !== undefined && existingBinding !== binding)
      ) {
        throw new Error("Shader stages expose conflicting uniform bindings");
      }
      byBinding.set(binding, uniform);
      bindingByName.set(uniform.name, binding);
    }
  }
  return [...byBinding.values()].sort((left, right) => (
    left.group - right.group || left.binding - right.binding
  ));
}

function createInitializedBuffer(device, data, usage, label) {
  const buffer = device.createBuffer({
    label,
    size: data.byteLength,
    usage,
    mappedAtCreation: true
  });
  new Uint8Array(buffer.getMappedRange()).set(new Uint8Array(data));
  buffer.unmap();
  return buffer;
}

function createGeometryBinding(device, geometry) {
  if (!geometry) {
    return {
      vertexBuffer: null,
      indexBuffer: null,
      vertexCount: 3,
      indexCount: 0
    };
  }
  validateShaderGeometryDescriptor(geometry, true);
  const vertexBuffer = createInitializedBuffer(
    device,
    geometry.data,
    GPUBufferUsage.VERTEX,
    "DynLex shader geometry"
  );
  let indexBuffer = null;
  try {
    if (geometry.indices) {
      indexBuffer = createInitializedBuffer(
        device,
        geometry.indices.data,
        GPUBufferUsage.INDEX,
        "DynLex shader indices"
      );
    }
    return {
      vertexBuffer,
      indexBuffer,
      vertexCount: geometry.vertexCount,
      indexCount: geometry.indices?.count ?? 0
    };
  } catch (error) {
    vertexBuffer.destroy();
    throw error;
  }
}

function destroyGeometryBinding(binding) {
  binding.indexBuffer?.destroy();
  binding.vertexBuffer?.destroy();
}

function destroyPass(pass) {
  for (const uniform of pass.uniforms) uniform.buffer.destroy();
  destroyGeometryBinding(pass);
}

function destroyProgramState(state) {
  if (!state) return;
  for (const pass of state.passes) destroyPass(pass);
}

function replacePassGeometry(device, pass, geometry) {
  const nextBinding = createGeometryBinding(device, geometry);
  destroyGeometryBinding(pass);
  Object.assign(pass, nextBinding);
}

async function checkedShaderModule(device, code, label) {
  const module = device.createShaderModule({ code, label });
  const compilation = await module.getCompilationInfo();
  const errors = compilation.messages.filter((message) => message.type === "error");
  if (errors.length > 0) {
    console.error(`${label} compilation failed`, compilation.messages);
    throw new Error("Shader preview could not compile. Check the browser log.");
  }
  return module;
}

function blendState(mode) {
  if (mode === "opaque") return undefined;
  return {
    color: { operation: "add", srcFactor: "one", dstFactor: "one" },
    alpha: { operation: "add", srcFactor: "one", dstFactor: "one" }
  };
}

function createUniformBindings(device, pipeline, reflectedUniforms) {
  const uniforms = [];
  const entriesByGroup = new Map();
  try {
    for (const uniform of reflectedUniforms) {
      const buffer = device.createBuffer({
        label: `DynLex '${uniform.name}' uniform`,
        size: 16,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
      });
      const binding = { ...uniform, buffer };
      uniforms.push(binding);
      const entries = entriesByGroup.get(uniform.group) ?? [];
      entries.push({ binding: uniform.binding, resource: { buffer } });
      entriesByGroup.set(uniform.group, entries);
    }
    const bindGroups = [...entriesByGroup]
      .sort(([left], [right]) => left - right)
      .map(([group, entries]) => ({
        group,
        bindGroup: device.createBindGroup({
          label: `DynLex shader resource group ${group}`,
          layout: pipeline.getBindGroupLayout(group),
          entries
        })
      }));
    return { uniforms, bindGroups };
  } catch (error) {
    for (const uniform of uniforms) uniform.buffer.destroy();
    throw error;
  }
}

async function createProgramPass(
  device,
  format,
  vertexSource,
  fragmentSource,
  reflectedUniforms,
  geometry,
  render,
  renderPass
) {
  const [vertexModule, fragmentModule] = await Promise.all([
    checkedShaderModule(device, vertexSource, "DynLex vertex shader"),
    checkedShaderModule(device, fragmentSource, "DynLex fragment shader")
  ]);
  let pipeline;
  try {
    pipeline = await device.createRenderPipelineAsync({
      label: "DynLex shader pipeline",
      layout: "auto",
      vertex: {
        module: vertexModule,
        entryPoint: "main",
        buffers: geometry
          ? [{
              arrayStride: 4 * Float32Array.BYTES_PER_ELEMENT,
              attributes: [{ shaderLocation: 0, offset: 0, format: "float32x4" }]
            }]
          : []
      },
      fragment: {
        module: fragmentModule,
        entryPoint: "main",
        targets: [{ format, blend: blendState(render.blendMode) }]
      },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: depthFormat,
        depthWriteEnabled: render.depthTest,
        depthCompare: render.depthTest ? "less" : "always",
        stencilFront: { compare: "equal" },
        stencilBack: { compare: "equal" },
        stencilReadMask: 0xff
      }
    });
  } catch (error) {
    console.error("WebGPU render pipeline creation failed", error);
    throw new Error("Shader preview could not create a pipeline. Check the browser log.");
  }

  const geometryBinding = createGeometryBinding(device, geometry);
  try {
    const resources = createUniformBindings(device, pipeline, reflectedUniforms);
    return {
      pipeline,
      ...resources,
      ...geometryBinding,
      renderPass
    };
  } catch (error) {
    destroyGeometryBinding(geometryBinding);
    throw error;
  }
}

function validateProgramDescriptor(programDescriptor) {
  if (
    !programDescriptor
    || typeof programDescriptor.fragmentSource !== "string"
    || programDescriptor.fragmentSource.length === 0
    || !Array.isArray(programDescriptor.fragmentUniforms)
    || (programDescriptor.vertexSource !== undefined
      && (typeof programDescriptor.vertexSource !== "string"
        || programDescriptor.vertexSource.length === 0))
    || Boolean(programDescriptor.vertexSource) !== Boolean(programDescriptor.geometry)
    || Boolean(programDescriptor.vertexSource) !== Array.isArray(programDescriptor.vertexUniforms)
  ) {
    throw new Error("Compiler returned an invalid shader program descriptor");
  }
  validateStageUniforms(programDescriptor.fragmentUniforms);
  if (programDescriptor.vertexUniforms) {
    validateStageUniforms(programDescriptor.vertexUniforms);
  }
}

export async function createWebGpuShaderPreview(canvas, options = {}) {
  if (!(canvas instanceof HTMLCanvasElement)) {
    throw new Error("Shader preview requires a canvas");
  }
  const context = canvas.getContext("webgpu");
  if (!context) {
    throw new Error("This canvas does not provide a WebGPU context");
  }
  const device = await requestWebGpuDevice();
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: "opaque" });

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
  let preparedState = null;
  let replacementGeneration = 0;
  let revision = 0;
  let running = options.running !== false;
  let frameRequest = 0;
  let pendingSubmissions = 0;
  let renderedFrameWaiters = [];
  const submittedFrameWaiterBatches = new Set();
  let attachments = null;
  let destroyed = false;
  let deviceLost = false;
  let stopObservingDeviceLoss = null;
  let transitionBounds = null;
  let snapshotRequired = false;
  let transitionCompositor = null;

  function scheduleFrame() {
    if (
      !destroyed
      && !deviceLost
      && !frameRequest
      && pendingSubmissions < maximumPendingSubmissions
    ) {
      frameRequest = requestAnimationFrame(drawFrame);
    }
  }

  function rejectFrameWaiters(error) {
    const waiters = renderedFrameWaiters;
    renderedFrameWaiters = [];
    for (const batch of submittedFrameWaiterBatches) waiters.push(...batch);
    submittedFrameWaiterBatches.clear();
    for (const waiter of waiters) waiter.reject(error);
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

  function destroyAttachments() {
    attachments?.depth.destroy();
    attachments = null;
  }

  function currentAttachments() {
    if (attachments?.width === canvas.width && attachments?.height === canvas.height) {
      return attachments;
    }
    destroyAttachments();
    attachments = {
      width: canvas.width,
      height: canvas.height,
      depth: device.createTexture({
        label: "DynLex depth buffer",
        size: [canvas.width, canvas.height],
        format: depthFormat,
        usage: GPUTextureUsage.RENDER_ATTACHMENT
      })
    };
    return attachments;
  }

  function currentGeometryHorizontalPixels() {
    const horizontalPixels = options.geometryHorizontalPixels
      ? options.geometryHorizontalPixels()
      : canvas.width;
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

  async function compileProgram(programDescriptor) {
    if (destroyed) throw new Error("Shader preview has been destroyed");
    if (deviceLost) throw new Error("Shader preview is unavailable because its WebGPU device was lost");
    validateProgramDescriptor(programDescriptor);
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
    const passPromises = [];
    if (!geometry || geometry.render.backgroundPass) {
      passPromises.push(createProgramPass(
        device,
        format,
        fullscreenVertexSource,
        programDescriptor.fragmentSource,
        programDescriptor.fragmentUniforms,
        null,
        { blendMode: "opaque", depthTest: false },
        0
      ));
    }
    if (geometry) {
      passPromises.push(createProgramPass(
        device,
        format,
        programDescriptor.vertexSource,
        programDescriptor.fragmentSource,
        mergePassUniforms(
          programDescriptor.fragmentUniforms,
          programDescriptor.vertexUniforms
        ),
        geometry,
        geometry.render,
        geometry.render.backgroundPass ? 1 : 0
      ));
    }

    let passes;
    try {
      passes = await Promise.all(passPromises);
    } catch (error) {
      const completed = await Promise.allSettled(passPromises);
      for (const result of completed) {
        if (result.status === "fulfilled") destroyPass(result.value);
      }
      throw error;
    }
    if (destroyed) {
      destroyProgramState({ passes });
      return null;
    }
    const geometryPass = geometry ? passes.at(-1) : null;
    return { passes, geometrySource, geometryPass, geometryHorizontalPixels };
  }

  async function replaceProgram(programDescriptor) {
    const generation = ++replacementGeneration;
    const state = await compileProgram(programDescriptor);
    if (!state || generation !== replacementGeneration || destroyed) {
      destroyProgramState(state);
      return false;
    }
    const previousState = activeState;
    activeState = state;
    destroyProgramState(preparedState);
    preparedState = null;
    transitionBounds = null;
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    updateGeometryDataset(
      state.geometryPass,
      state.geometryHorizontalPixels
    );
    canvas.dataset.previewState = "ready";
    destroyProgramState(previousState);
    scheduleFrame();
    return true;
  }

  async function prepareProgram(programDescriptor) {
    const generation = ++replacementGeneration;
    const existingCompositor = transitionCompositor;
    const compositorPromise = existingCompositor
      ? Promise.resolve(existingCompositor)
      : createTransitionCompositor(device, format, depthFormat);
    let state;
    let compositor;
    try {
      [state, compositor] = await Promise.all([
        compileProgram(programDescriptor),
        compositorPromise
      ]);
    } catch (error) {
      if (!existingCompositor) {
        const createdCompositor = await compositorPromise.catch(() => null);
        createdCompositor?.destroy();
      }
      throw error;
    }
    if (!state || generation !== replacementGeneration || destroyed) {
      destroyProgramState(state);
      if (!existingCompositor) compositor.destroy();
      return false;
    }
    transitionCompositor = compositor;
    destroyProgramState(preparedState);
    preparedState = state;
    canvas.dataset.previewPreparedRevision = String(revision + 1);
    canvas.dataset.previewPreparedGeometryVertices = String(state.geometryPass?.vertexCount ?? 0);
    if (state.geometryHorizontalPixels === null) {
      delete canvas.dataset.previewPreparedGeometryHorizontalPixels;
    } else {
      canvas.dataset.previewPreparedGeometryHorizontalPixels = String(state.geometryHorizontalPixels);
    }
    return true;
  }

  function resizeGeneratedGeometry(state, prepared) {
    if (!isGeneratedTerrainGeometryDescriptor(state?.geometrySource)) return;
    const horizontalPixels = currentGeometryHorizontalPixels();
    if (horizontalPixels === state.geometryHorizontalPixels) return;
    const geometry = resolveTerrainGeometryDescriptor(state.geometrySource, horizontalPixels);
    replacePassGeometry(device, state.geometryPass, geometry);
    state.geometryHorizontalPixels = horizontalPixels;
    if (prepared) {
      canvas.dataset.previewPreparedGeometryVertices = String(geometry.vertexCount);
      canvas.dataset.previewPreparedGeometryHorizontalPixels = String(horizontalPixels);
    } else {
      updateGeometryDataset(geometry, horizontalPixels);
    }
  }

  function frameFor(timestamp, role) {
    return {
      elapsedSeconds: elapsedSeconds(timestamp, role),
      width: canvas.width,
      height: canvas.height
    };
  }

  function encodeState(renderPass, state, frame) {
    renderPass.setStencilReference(1);
    for (const pass of state.passes) {
      renderPass.setPipeline(pass.pipeline);
      for (const uniform of pass.uniforms) {
        const value = uniformValueProviders[uniform.name](frame, pass.renderPass);
        device.queue.writeBuffer(uniform.buffer, 0, new Float32Array([value, 0, 0, 0]));
      }
      for (const binding of pass.bindGroups) {
        renderPass.setBindGroup(binding.group, binding.bindGroup);
      }
      if (pass.vertexBuffer) renderPass.setVertexBuffer(0, pass.vertexBuffer);
      if (pass.indexBuffer) {
        renderPass.setIndexBuffer(pass.indexBuffer, "uint32");
        renderPass.drawIndexed(pass.indexCount);
      } else {
        renderPass.draw(pass.vertexCount);
      }
    }
  }

  function depthAttachment(targets, stencilClearValue) {
    return {
      view: targets.depth.createView(),
      depthClearValue: 1,
      depthLoadOp: "clear",
      depthStoreOp: "discard",
      stencilClearValue,
      stencilLoadOp: "clear",
      stencilStoreOp: "discard"
    };
  }

  function drawFrame(timestamp) {
    frameRequest = 0;
    if (destroyed) return;
    resizeDrawingBuffer();
    if (activeState) {
      resizeGeneratedGeometry(activeState, false);
      resizeGeneratedGeometry(preparedState, true);
      const activeFrame = frameFor(timestamp, "active");
      const targets = currentAttachments();
      const encoder = device.createCommandEncoder({ label: "DynLex shader frame" });
      if (transitionBounds) {
        if (!preparedState) throw new Error("Shader transition has no prepared program");
        if (!transitionCompositor) throw new Error("Shader transition compositor is unavailable");
        if (transitionCompositor.resize(canvas.width, canvas.height)) snapshotRequired = true;
        if (snapshotRequired) {
          const snapshotPass = encoder.beginRenderPass({
            colorAttachments: [{
              view: transitionCompositor.snapshotView(),
              clearValue: { r: 0, g: 0, b: 0, a: 1 },
              loadOp: "clear",
              storeOp: "store"
            }],
            depthStencilAttachment: depthAttachment(targets, 1)
          });
          encodeState(snapshotPass, activeState, activeFrame);
          snapshotPass.end();
          snapshotRequired = false;
        }
      }
      const renderPass = encoder.beginRenderPass({
        colorAttachments: [{
          view: context.getCurrentTexture().createView(),
          clearValue: { r: 0, g: 0, b: 0, a: 1 },
          loadOp: "clear",
          storeOp: "store"
        }],
        depthStencilAttachment: depthAttachment(targets, transitionBounds ? 0 : 1)
      });
      if (transitionBounds) {
        transitionCompositor.encode(renderPass, transitionBounds, () => {
          encodeState(renderPass, preparedState, frameFor(timestamp, "prepared"));
        });
      } else {
        encodeState(renderPass, activeState, activeFrame);
      }
      renderPass.end();
      device.queue.submit([encoder.finish()]);
      pendingSubmissions += 1;
      canvas.dataset.previewElapsedSeconds = String(
        transitionBounds ? elapsedSeconds(timestamp, "prepared") : activeFrame.elapsedSeconds
      );
      const waiters = renderedFrameWaiters;
      renderedFrameWaiters = [];
      if (waiters.length > 0) {
        submittedFrameWaiterBatches.add(waiters);
      }
      device.queue.onSubmittedWorkDone().then(() => {
        if (destroyed || deviceLost) return;
        pendingSubmissions -= 1;
        if (waiters.length > 0 && submittedFrameWaiterBatches.delete(waiters)) {
          for (const waiter of waiters) waiter.resolve();
        }
        if (running || renderedFrameWaiters.length > 0) scheduleFrame();
      }, (error) => {
        console.error("WebGPU frame submission failed", error);
        pendingSubmissions -= 1;
        if (waiters.length > 0 && submittedFrameWaiterBatches.delete(waiters)) {
          const failure = new Error("Shader preview could not render. Check the browser log.");
          for (const waiter of waiters) waiter.reject(failure);
        }
      });
    }
    if (running) scheduleFrame();
  }

  function setRunning(nextRunning) {
    if (destroyed) throw new Error("Shader preview has been destroyed");
    if (deviceLost) throw new Error("Shader preview is unavailable because its WebGPU device was lost");
    running = Boolean(nextRunning);
    if (running) scheduleFrame();
  }

  function beginTransition() {
    if (!preparedState) throw new Error("Shader transition requires a prepared program");
    snapshotRequired = true;
    canvas.dataset.previewTransitionState = "active";
  }

  function setTransition(bounds) {
    if (!preparedState) throw new Error("Shader transition requires a prepared program");
    if (
      !bounds
      || !["left", "top", "width", "height", "frameWidth", "frameHeight"].every(
        (name) => typeof bounds[name] === "number" && Number.isFinite(bounds[name])
      )
      || bounds.width <= 0
      || bounds.height <= 0
      || bounds.frameWidth <= 0
      || bounds.frameHeight <= 0
    ) {
      throw new Error("Shader transition bounds are invalid");
    }
    transitionBounds = bounds;
    scheduleFrame();
  }

  function promotePrepared() {
    if (!preparedState) throw new Error("Shader transition has no prepared program to promote");
    destroyProgramState(activeState);
    activeState = preparedState;
    preparedState = null;
    transitionBounds = null;
    snapshotRequired = false;
    revision += 1;
    canvas.dataset.previewRevision = String(revision);
    updateGeometryDataset(activeState.geometryPass, activeState.geometryHorizontalPixels);
    delete canvas.dataset.previewPreparedRevision;
    delete canvas.dataset.previewPreparedGeometryVertices;
    delete canvas.dataset.previewPreparedGeometryHorizontalPixels;
    delete canvas.dataset.previewTransitionState;
  }

  function discardPrepared() {
    destroyProgramState(preparedState);
    preparedState = null;
    transitionBounds = null;
    snapshotRequired = false;
    delete canvas.dataset.previewPreparedRevision;
    delete canvas.dataset.previewPreparedGeometryVertices;
    delete canvas.dataset.previewPreparedGeometryHorizontalPixels;
    delete canvas.dataset.previewTransitionState;
  }

  function whenNextFrameRendered() {
    if (!activeState) {
      throw new Error("Shader preview cannot render before a program is installed");
    }
    const rendered = new Promise((resolve, reject) => {
      renderedFrameWaiters.push({ resolve, reject });
    });
    scheduleFrame();
    return rendered;
  }

  function destroy() {
    if (destroyed) return;
    destroyed = true;
    stopObservingDeviceLoss();
    replacementGeneration += 1;
    cancelAnimationFrame(frameRequest);
    frameRequest = 0;
    destroyProgramState(activeState);
    activeState = null;
    destroyProgramState(preparedState);
    preparedState = null;
    destroyAttachments();
    transitionCompositor?.destroy();
    transitionCompositor = null;
    context.unconfigure();
    canvas.dataset.previewState = "destroyed";
    rejectFrameWaiters(new Error("Shader preview was destroyed before rendering completed"));
  }

  stopObservingDeviceLoss = observeWebGpuDeviceLoss(device, () => {
    if (destroyed) return;
    deviceLost = true;
    replacementGeneration += 1;
    canvas.dataset.previewState = "lost";
    running = false;
    cancelAnimationFrame(frameRequest);
    frameRequest = 0;
    destroyProgramState(activeState);
    activeState = null;
    destroyProgramState(preparedState);
    preparedState = null;
    destroyAttachments();
    transitionCompositor?.destroy();
    transitionCompositor = null;
    rejectFrameWaiters(new Error("Shader preview could not render. Check the browser log."));
  });

  canvas.dataset.previewApi = "webgpu";
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
