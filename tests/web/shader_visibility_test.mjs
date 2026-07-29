import assert from "node:assert/strict";
import { evaluate, waitFor } from "./browser_test_driver.mjs";

export async function verifyOffscreenRevealReturn(incomingTimeBinding) {
  const hiddenRevealProgress = await evaluate(`new Promise((resolve) => {
    const section = document.querySelector('[data-live-shader-banner]');
    const observer = new MutationObserver(() => {
      if (
        section.dataset.scenePhase !== 'next-thought'
        || section.dataset.incomingShaderIndex !== '1'
      ) {
        return;
      }
      observer.disconnect();
      document.querySelector('#sketches').scrollIntoView({ behavior: 'instant' });
      requestAnimationFrame(() => resolve(section.dataset.sceneProgress));
    });
    observer.observe(section, {
      attributes: true,
      attributeFilter: ['data-scene-progress']
    });
  })`);
  assert.ok(
    Number(hiddenRevealProgress) >= 0.79 && Number(hiddenRevealProgress) < 0.99,
    `The banner must leave during the middle of the thought reveal: ${hiddenRevealProgress}`
  );
  await waitFor(
    `document.querySelector('[data-live-shader-banner]').getBoundingClientRect().bottom <= 0`,
    "the revealing shader banner to leave the viewport"
  );
  await new Promise((resolve) => setTimeout(resolve, 250));
  assert.equal(
    await evaluate("document.querySelector('[data-live-shader-banner]').dataset.sceneProgress"),
    hiddenRevealProgress,
    "The reveal timeline must not mutate its clip geometry while the banner is off-screen"
  );
  assert.equal(
    await evaluate("document.querySelector('[data-live-shader-banner]').dataset.incomingShaderIndex"),
    "1",
    "The shader must still be revealing before returning to the banner"
  );

  const returningRevealState = await evaluate(`(async () => {
    const section = document.querySelector('[data-live-shader-banner]');
    const revealingLayer = section.querySelector('[data-layer-state="revealing"]');
    const canvas = revealingLayer.querySelector('canvas');
    const gl = canvas.getContext('webgl2');
    const timeBuffer = gl.getIndexedParameter(
      gl.UNIFORM_BUFFER_BINDING,
      ${incomingTimeBinding}
    );
    if (!timeBuffer) throw new Error('The incoming shader time buffer is not bound');
    const readTime = () => {
      const value = new Float32Array(1);
      gl.bindBuffer(gl.UNIFORM_BUFFER, timeBuffer);
      gl.getBufferSubData(gl.UNIFORM_BUFFER, 0, value);
      return value[0];
    };
    const first = readTime();
    window.scrollTo({ top: 0, behavior: 'instant' });
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const currentActiveLayer = section.querySelector('[data-layer-state="active"]');
    const currentRevealingLayer = section.querySelector('[data-layer-state="revealing"]');
    if (!currentRevealingLayer) {
      throw new Error('The revealing shader was promoted before its resumed frame was observable');
    }
    const activeStyle = getComputedStyle(currentActiveLayer);
    const revealingStyle = getComputedStyle(currentRevealingLayer);
    const layerRect = currentRevealingLayer.getBoundingClientRect();
    const canvasRect = canvas.getBoundingClientRect();
    return {
      activeClipPath: activeStyle.clipPath,
      activeFilter: activeStyle.filter,
      revealingClipPath: revealingStyle.clipPath,
      revealingFilter: revealingStyle.filter,
      revealingLayerState: currentRevealingLayer.dataset.layerState,
      bannerVisible: section.getBoundingClientRect().bottom > 0,
      progress: Number(section.dataset.sceneProgress),
      maskTransform: section.querySelector(
        '[data-thought-cloud-path="' + revealingLayer.dataset.shaderLayer + '"]'
      ).getAttribute('transform'),
      canvasMatchesLayer: (
        Math.abs(canvasRect.left - layerRect.left) <= 1
        && Math.abs(canvasRect.top - layerRect.top) <= 1
        && Math.abs(canvasRect.width - layerRect.width) <= 1
        && Math.abs(canvasRect.height - layerRect.height) <= 1
      ),
      backingWidth: canvas.width,
      backingHeight: canvas.height,
      requiredBackingWidth: Math.round(canvas.clientWidth * (window.devicePixelRatio || 1)),
      requiredBackingHeight: Math.round(canvas.clientHeight * (window.devicePixelRatio || 1)),
      viewport: [...gl.getParameter(gl.VIEWPORT)],
      shaderMoves: readTime() > first
    };
  })()`);

  assert.equal(returningRevealState.activeClipPath, "none");
  assert.equal(returningRevealState.activeFilter, "none");
  assert.match(returningRevealState.revealingClipPath, /thought-cloud-mask-[01]/);
  assert.notEqual(returningRevealState.revealingFilter, "none");
  assert.equal(returningRevealState.revealingLayerState, "revealing");
  assert.equal(returningRevealState.bannerVisible, true);
  assert.equal(
    returningRevealState.progress,
    Number(hiddenRevealProgress),
    `The paused reveal must render before its timeline resumes: ${JSON.stringify(returningRevealState)}`
  );
  assert.match(returningRevealState.maskTransform, /^translate\(.+\) scale\(.+\)$/);
  assert.equal(returningRevealState.canvasMatchesLayer, true);
  assert.ok(returningRevealState.backingWidth >= returningRevealState.requiredBackingWidth);
  assert.ok(returningRevealState.backingHeight >= returningRevealState.requiredBackingHeight);
  assert.deepEqual(
    returningRevealState.viewport,
    [0, 0, returningRevealState.backingWidth, returningRevealState.backingHeight]
  );
  assert.equal(
    returningRevealState.shaderMoves,
    true,
    "The revealing shader must resume rendering after returning to the banner"
  );
}
