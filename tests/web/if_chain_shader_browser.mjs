import assert from "node:assert/strict";
import { closeBrowserSession, evaluate, navigate } from "./browser_test_driver.mjs";
import { compileIfChainShaders } from "./if_chain_shader.mjs";

try {
  const shaders = await compileIfChainShaders(process.env.DYNLEX_TEST_COMPILER, process.env.DYNLEX_TEST_BROWSER_BUILD);
  await navigate("/wgsl-translator.js");
  for (const shader of shaders) {
    const pixels = await evaluate(`(async () => {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) throw new Error('WebGPU adapter is missing');
      const device = await adapter.requestDevice();
      try {
        const pipeline = await device.createRenderPipelineAsync({
          layout: 'auto',
          vertex: { module: device.createShaderModule({ code: \`
            @vertex fn main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
              let positions = array<vec2<f32>, 3>(vec2<f32>(-1, -1), vec2<f32>(3, -1), vec2<f32>(-1, 3));
              return vec4<f32>(positions[index], 0, 1);
            }\` }), entryPoint: 'main' },
          fragment: { module: device.createShaderModule({ code: ${JSON.stringify(shader.webgpu)} }),
            entryPoint: 'main', targets: [{ format: 'rgba8unorm' }] },
          primitive: { topology: 'triangle-list' }
        });
        const texture = device.createTexture({ size: [4, 1], format: 'rgba8unorm',
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
        const buffer = device.createBuffer({ size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{ view: texture.createView(),
          clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' }] });
        pass.setPipeline(pipeline);
        pass.draw(3);
        pass.end();
        encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow: 256 }, [4, 1]);
        device.queue.submit([encoder.finish()]);
        await buffer.mapAsync(GPUMapMode.READ);
        const bytes = new Uint8Array(buffer.getMappedRange());
        const pixels = Array.from({ length: 4 }, (_, index) => Array.from(bytes.slice(index * 4, index * 4 + 4)));
        buffer.unmap();
        buffer.destroy();
        texture.destroy();
        return pixels;
      } finally {
        device.destroy();
      }
    })()`);
    const glPixels = await evaluate(`(() => {
      const gl = new OffscreenCanvas(4, 1).getContext('webgl2');
      if (!gl) throw new Error('WebGL2 context is missing');
      const compile = (type, source) => {
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
        return shader;
      };
      const vertex = compile(gl.VERTEX_SHADER, \`#version 300 es
        void main() {
          vec2 positions[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
          gl_Position = vec4(positions[gl_VertexID], 0, 1);
        }\`);
      const fragment = compile(gl.FRAGMENT_SHADER, ${JSON.stringify(shader.webgl)});
      const program = gl.createProgram();
      gl.attachShader(program, vertex);
      gl.attachShader(program, fragment);
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
      gl.useProgram(program);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      const bytes = new Uint8Array(16);
      gl.readPixels(0, 0, 4, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
      if (gl.getError() !== gl.NO_ERROR) throw new Error('WebGL readback failed');
      gl.deleteProgram(program);
      gl.deleteShader(vertex);
      gl.deleteShader(fragment);
      gl.getExtension('WEBGL_lose_context').loseContext();
      return Array.from({ length: 4 }, (_, index) => Array.from(bytes.slice(index * 4, index * 4 + 4)));
    })()`);
    for (const [backend, actual] of [["WebGPU", pixels], ["WebGL", glPixels]]) {
      for (let index = 0; index < 4; index++) {
        for (let channel = 0; channel < 3; channel++) {
          assert.ok(Math.abs(actual[index][channel] - shader.expected[index]) <= 1,
            `${shader.name} ${backend}, pixel ${index}: expected ${shader.expected[index]}, got ${actual[index]}`);
        }
        assert.equal(actual[index][3], 255);
      }
    }
  }
  console.log(`Rendered every branch correctly in ${shaders.length} runtime if-chain shaders.`);
} finally {
  closeBrowserSession();
}
