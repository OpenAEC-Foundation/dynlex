import assert from 'node:assert/strict';
import { requestGraphicsBackend, createWebGlProgram } from '../../web/graphics-backend.js';

const original = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
const originalWarn = console.warn;
const warnings = [];
console.warn = (...args) => warnings.push(args);
const use = gpu => Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { gpu } });
try {
  use(undefined);
  assert.deepEqual(await requestGraphicsBackend(), { backend: 'webgl' });
  use({ async requestAdapter() { return null; } });
  assert.deepEqual(await requestGraphicsBackend(), { backend: 'webgl' });
  const adapterError = new Error('adapter unavailable');
  use({ async requestAdapter() { throw adapterError; } });
  assert.deepEqual(await requestGraphicsBackend(), { backend: 'webgl' });
  assert.equal(warnings.at(-1)[1], adapterError, 'Adapter failure must remain in the log');
  const deviceError = new Error('device unavailable');
  use({ async requestAdapter() { return { async requestDevice() { throw deviceError; } }; } });
  assert.deepEqual(await requestGraphicsBackend(), { backend: 'webgl' });
  assert.equal(warnings.at(-1)[1], deviceError, 'Device failure must remain in the log');
  const device = {};
  let requests = 0;
  use({ async requestAdapter(options) {
    assert.deepEqual(options, { powerPreference: 'high-performance' });
    return { async requestDevice() { requests++; return device; } };
  } });
  assert.deepEqual(await requestGraphicsBackend(), { backend: 'webgpu', device });
  assert.equal(requests, 1, 'Selection must hand over the acquired device without a second request');
  assert.equal(warnings.length, 2, 'Normal capability absence is not an initialization failure');
} finally {
  if (original) Object.defineProperty(globalThis, 'navigator', original);
  else delete globalThis.navigator;
  console.warn = originalWarn;
}
console.log('Graphics backend selection: WebGPU preference, absent API, null adapter and request failures passed.');

const originalError = console.error;
const details = [];
console.error = (...args) => details.push(args);
try {
  for (const failure of ['vertex', 'fragment', 'link', null]) {
    const liveShaders = new Set(), livePrograms = new Set();
    const gl = {
      VERTEX_SHADER: 'vertex', FRAGMENT_SHADER: 'fragment',
      createShader(type) { const shader = { type }; liveShaders.add(shader); return shader; },
      shaderSource() {}, compileShader() {},
      getShaderParameter(shader) { return shader.type !== failure; },
      getShaderInfoLog() { return 'private compiler details'; },
      deleteShader(shader) { liveShaders.delete(shader); },
      createProgram() { const program = {}; livePrograms.add(program); return program; },
      attachShader() {}, linkProgram() {},
      getProgramParameter() { return failure !== 'link'; },
      getProgramInfoLog() { return 'private linker details'; },
      deleteProgram(program) { livePrograms.delete(program); }
    };
    if (failure) {
      assert.throws(() => createWebGlProgram(gl, 'vertex', 'fragment'), /^Error: An error occurred\. Check the log\.$/);
      assert.equal(livePrograms.size, 0, 'A failed program must release its resources');
    } else {
      const program = createWebGlProgram(gl, 'vertex', 'fragment');
      assert(livePrograms.has(program));
      gl.deleteProgram(program);
    }
    assert.equal(liveShaders.size, 0, 'Successful and failed compilation must release shader handles');
  }
  assert.equal(details.length, 3, 'Compilation/link failures must log details');
} finally { console.error = originalError; }
console.log('Shared WebGL program creation: failure cleanup and generic client errors passed.');
