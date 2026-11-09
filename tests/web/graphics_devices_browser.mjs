import assert from 'node:assert/strict';
import { evaluate, navigate } from './browser_test_driver.mjs';

export async function verifyGraphicsDevices() {
  await navigate('/wiki/index.html');
  const result = await evaluate(`(async () => {
    const { requestGraphicsBackend } = await import('/graphics-backend.js');
    const graphics = await requestGraphicsBackend();
    if (graphics.backend !== 'webgpu') return { backend: graphics.backend };
    const { device } = graphics;
    try {
      const canvas = document.createElement('canvas');
      document.body.append(canvas);
      const context = canvas.getContext('webgpu');
      context.configure({ device, format: navigator.gpu.getPreferredCanvasFormat() });
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginRenderPass({ colorAttachments: [{
        view: context.getCurrentTexture().createView(),
        clearValue: [0.1, 0.2, 0.3, 1], loadOp: 'clear', storeOp: 'store'
      }] });
      pass.end(); device.queue.submit([encoder.finish()]);
      await device.queue.onSubmittedWorkDone();
      const other = document.createElement('canvas');
      document.body.append(other);
      const gl = other.getContext('webgl2');
      if (!gl) return { backend: graphics.backend, webgl: false };
      gl.clearColor(0.4, 0.3, 0.2, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      return { backend: graphics.backend, webgl: true, glError: gl.getError() };
    } finally { device.destroy(); }
  })()`);
  assert.equal(result.backend, 'webgpu', 'The available WebGPU device must be preferred');
  assert.equal(result.webgl, true, 'The homepage also requires WebGL2 for the farm renderer');
  assert.equal(result.glError, 0);
}
