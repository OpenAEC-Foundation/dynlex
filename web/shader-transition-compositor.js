const CLOUD_SEGMENTS = Object.freeze([
  [0.16, 0.78, 0.05, 0.75, 0.01, 0.62, 0.08, 0.52],
  [0.08, 0.52, 0.02, 0.39, 0.10, 0.25, 0.23, 0.25],
  [0.23, 0.25, 0.28, 0.09, 0.45, 0.04, 0.56, 0.15],
  [0.56, 0.15, 0.68, 0.05, 0.85, 0.12, 0.86, 0.28],
  [0.86, 0.28, 1.00, 0.31, 1.00, 0.49, 0.94, 0.58],
  [0.94, 0.58, 1.00, 0.72, 0.88, 0.86, 0.75, 0.82],
  [0.75, 0.82, 0.66, 0.94, 0.48, 0.94, 0.40, 0.84],
  [0.40, 0.84, 0.31, 0.91, 0.20, 0.88, 0.16, 0.78]
]);

const maskShader = `
struct Cloud { rect: vec4<f32> }
@group(0) @binding(0) var<uniform> cloud: Cloud;

struct VertexOutput { @builtin(position) position: vec4<f32> }

@vertex
fn vertexMain(@location(0) point: vec2<f32>) -> VertexOutput {
  let pixel = cloud.rect.xy + point * cloud.rect.zw;
  return VertexOutput(vec4<f32>(pixel.x * 2.0 - 1.0, 1.0 - pixel.y * 2.0, 0.0, 1.0));
}

@fragment
fn fragmentMain() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0);
}
`;

const compositeShader = `
@group(0) @binding(0) var frozenFrame: texture_2d<f32>;
@group(0) @binding(1) var frozenSampler: sampler;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>
}

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> VertexOutput {
  let positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0)
  );
  let position = positions[index];
  return VertexOutput(vec4<f32>(position, 0.0, 1.0), position * vec2<f32>(0.5, -0.5) + 0.5);
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
  return textureSample(frozenFrame, frozenSampler, input.uv);
}
`;

const glowShader = `
struct Cloud { rect: vec4<f32> }
@group(0) @binding(0) var<uniform> cloud: Cloud;

struct VertexOutput { @builtin(position) position: vec4<f32> }

@vertex
fn vertexMain(@location(0) point: vec2<f32>) -> VertexOutput {
  let pixel = cloud.rect.xy + point * cloud.rect.zw;
  return VertexOutput(vec4<f32>(pixel.x * 2.0 - 1.0, 1.0 - pixel.y * 2.0, 0.0, 1.0));
}

@fragment
fn fragmentMain() -> @location(0) vec4<f32> {
  return vec4<f32>(0.41, 0.84, 1.0, 0.24);
}
`;

function cubic(segment, progress) {
  const inverse = 1 - progress;
  const [x0, y0, x1, y1, x2, y2, x3, y3] = segment;
  return [
    inverse ** 3 * x0 + 3 * inverse ** 2 * progress * x1
      + 3 * inverse * progress ** 2 * x2 + progress ** 3 * x3,
    inverse ** 3 * y0 + 3 * inverse ** 2 * progress * y1
      + 3 * inverse * progress ** 2 * y2 + progress ** 3 * y3
  ];
}

export function createCloudTriangleVertices() {
  const boundary = [];
  for (const segment of CLOUD_SEGMENTS) {
    for (let step = 0; step < 12; step += 1) boundary.push(cubic(segment, step / 12));
  }
  const values = new Float32Array(boundary.length * 6);
  for (let index = 0; index < boundary.length; index += 1) {
    const next = (index + 1) % boundary.length;
    values.set([0.5, 0.5, ...boundary[index], ...boundary[next]], index * 6);
  }
  return values;
}

function initializedBuffer(device, values, usage, label) {
  const buffer = device.createBuffer({ label, size: values.byteLength, usage, mappedAtCreation: true });
  new Uint8Array(buffer.getMappedRange()).set(new Uint8Array(values.buffer));
  buffer.unmap();
  return buffer;
}

export async function createTransitionCompositor(device, format, depthFormat) {
  const [maskModule, compositeModule, glowModule] = [
    device.createShaderModule({ code: maskShader, label: "Homepage cloud stencil shader" }),
    device.createShaderModule({ code: compositeShader, label: "Homepage snapshot shader" }),
    device.createShaderModule({ code: glowShader, label: "Homepage cloud glow shader" })
  ];
  const vertexData = createCloudTriangleVertices();
  const vertexBuffer = initializedBuffer(
    device,
    vertexData,
    GPUBufferUsage.VERTEX,
    "Homepage cloud geometry"
  );
  const maskUniform = device.createBuffer({
    label: "Homepage cloud mask bounds",
    size: 16,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });
  const glowUniform = device.createBuffer({
    label: "Homepage cloud glow bounds",
    size: 16,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });
  const vertexBuffers = [{
    arrayStride: 2 * Float32Array.BYTES_PER_ELEMENT,
    attributes: [{ shaderLocation: 0, offset: 0, format: "float32x2" }]
  }];
  const [maskPipeline, glowPipeline, compositePipeline] = await Promise.all([
    device.createRenderPipelineAsync({
      label: "Homepage cloud stencil pipeline",
      layout: "auto",
      vertex: { module: maskModule, entryPoint: "vertexMain", buffers: vertexBuffers },
      fragment: {
        module: maskModule,
        entryPoint: "fragmentMain",
        targets: [{ format, writeMask: 0 }]
      },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: depthFormat,
        depthWriteEnabled: false,
        depthCompare: "always",
        stencilFront: { compare: "always", passOp: "replace" },
        stencilBack: { compare: "always", passOp: "replace" },
        stencilWriteMask: 0xff
      }
    }),
    device.createRenderPipelineAsync({
      label: "Homepage cloud glow pipeline",
      layout: "auto",
      vertex: { module: glowModule, entryPoint: "vertexMain", buffers: vertexBuffers },
      fragment: {
        module: glowModule,
        entryPoint: "fragmentMain",
        targets: [{
          format,
          blend: {
            color: { operation: "add", srcFactor: "src-alpha", dstFactor: "one" },
            alpha: { operation: "add", srcFactor: "one", dstFactor: "one" }
          }
        }]
      },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: depthFormat,
        depthWriteEnabled: false,
        depthCompare: "always"
      }
    }),
    device.createRenderPipelineAsync({
      label: "Homepage snapshot pipeline",
      layout: "auto",
      vertex: { module: compositeModule, entryPoint: "vertexMain" },
      fragment: { module: compositeModule, entryPoint: "fragmentMain", targets: [{ format }] },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: depthFormat,
        depthWriteEnabled: false,
        depthCompare: "always"
      }
    })
  ]);
  const maskBindGroup = device.createBindGroup({
    layout: maskPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: maskUniform } }]
  });
  const glowBindGroup = device.createBindGroup({
    layout: glowPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: glowUniform } }]
  });
  const sampler = device.createSampler({ magFilter: "linear", minFilter: "linear" });
  let snapshot = null;
  let snapshotBindGroup = null;
  let destroyed = false;

  function resize(width, height) {
    if (snapshot?.width === width && snapshot?.height === height) return false;
    snapshot?.texture.destroy();
    const texture = device.createTexture({
      label: "Homepage outgoing scene snapshot",
      size: [width, height],
      format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING
    });
    snapshot = { width, height, texture };
    snapshotBindGroup = device.createBindGroup({
      layout: compositePipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: texture.createView() },
        { binding: 1, resource: sampler }
      ]
    });
    return true;
  }

  function snapshotView() {
    if (!snapshot) throw new Error("Homepage transition snapshot is not sized");
    return snapshot.texture.createView();
  }

  function writeBounds(buffer, bounds, glowPixels = 0) {
    const left = (bounds.left - glowPixels) / bounds.frameWidth;
    const top = (bounds.top - glowPixels) / bounds.frameHeight;
    const width = (bounds.width + glowPixels * 2) / bounds.frameWidth;
    const height = (bounds.height + glowPixels * 2) / bounds.frameHeight;
    device.queue.writeBuffer(buffer, 0, new Float32Array([left, top, width, height]));
  }

  function encode(pass, bounds, encodeIncoming) {
    writeBounds(maskUniform, bounds);
    writeBounds(glowUniform, bounds, 18);
    pass.setPipeline(compositePipeline);
    pass.setBindGroup(0, snapshotBindGroup);
    pass.draw(3);
    pass.setPipeline(glowPipeline);
    pass.setBindGroup(0, glowBindGroup);
    pass.setVertexBuffer(0, vertexBuffer);
    pass.draw(vertexData.length / 2);
    pass.setPipeline(maskPipeline);
    pass.setBindGroup(0, maskBindGroup);
    pass.setStencilReference(1);
    pass.setVertexBuffer(0, vertexBuffer);
    pass.draw(vertexData.length / 2);
    encodeIncoming(pass);
  }

  function destroy() {
    if (destroyed) return;
    destroyed = true;
    snapshot?.texture.destroy();
    vertexBuffer.destroy();
    maskUniform.destroy();
    glowUniform.destroy();
  }

  return Object.freeze({ resize, snapshotView, encode, destroy });
}
