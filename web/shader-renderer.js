import {
  createWebGpuShaderPreview,
  hasWebGpuDevice
} from "./shader-renderer-webgpu.js";
import { createWebGlShaderPreview } from "./shader-renderer-webgl.js";
import { validateShaderGeometryDescriptor } from "./shader-renderer-shared.js";

function sourceForBackend(program, backend) {
  const fragmentSource = program?.fragmentSources?.[backend];
  const vertexSource = program?.vertexSources?.[backend];
  return {
    ...program,
    fragmentSource,
    ...(program.vertexSources ? { vertexSource } : {})
  };
}

function selectedPreview(preview, backend) {
  return Object.freeze({
    backend,
    replaceProgram(program) {
      return preview.replaceProgram(sourceForBackend(program, backend));
    },
    prepareProgram(program) {
      return preview.prepareProgram(sourceForBackend(program, backend));
    },
    beginTransition: preview.beginTransition,
    setTransition: preview.setTransition,
    promotePrepared: preview.promotePrepared,
    discardPrepared: preview.discardPrepared,
    setRunning: preview.setRunning,
    whenNextFrameRendered: preview.whenNextFrameRendered,
    destroy: preview.destroy
  });
}

export async function createShaderPreview(canvas, options = {}) {
  const backend = navigator.gpu && await hasWebGpuDevice() ? "webgpu" : "webgl";
  const preview = backend === "webgpu"
    ? await createWebGpuShaderPreview(canvas, options)
    : createWebGlShaderPreview(canvas, options);
  return selectedPreview(preview, backend);
}

export { validateShaderGeometryDescriptor };
