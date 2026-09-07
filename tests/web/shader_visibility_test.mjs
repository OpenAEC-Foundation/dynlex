import assert from "node:assert/strict";
import { evaluate, waitFor } from "./browser_test_driver.mjs";

export async function verifyOffscreenRevealReturn() {
  const hiddenRevealState = await evaluate(`new Promise((resolve) => {
    const section = document.querySelector('[data-live-shader-banner]');
    const canvas = section.querySelector('[data-shader-canvas="immersive"]');
    const observer = new MutationObserver(() => {
      if (
        section.dataset.scenePhase !== 'next-thought'
        || section.dataset.incomingShaderIndex !== '1'
        || Number(section.dataset.sceneProgress) < 0.79
      ) return;
      observer.disconnect();
      const state = {
        progress: section.dataset.sceneProgress,
        shaderTime: canvas.dataset.previewElapsedSeconds,
        backingWidth: canvas.width,
        backingHeight: canvas.height
      };
      document.querySelector('#challenges').scrollIntoView({ behavior: 'instant' });
      requestAnimationFrame(() => resolve(state));
    });
    observer.observe(section, { attributes: true, attributeFilter: ['data-scene-progress'] });
  })`);
  assert.ok(
    Number(hiddenRevealState.progress) >= 0.79 && Number(hiddenRevealState.progress) < 0.99,
    `The banner must leave during the middle of the thought reveal: ${JSON.stringify(hiddenRevealState)}`
  );
  await waitFor(
    `document.querySelector('[data-live-shader-banner]').getBoundingClientRect().bottom <= 0`,
    "the transitioning shader banner to leave the viewport"
  );
  const stoppedShaderTime = await evaluate(
    "document.querySelector('[data-shader-canvas=\"immersive\"]').dataset.previewElapsedSeconds"
  );
  await new Promise((resolve) => setTimeout(resolve, 250));
  const pausedState = await evaluate(`(() => {
    const section = document.querySelector('[data-live-shader-banner]');
    const canvas = section.querySelector('[data-shader-canvas="immersive"]');
    return {
      progress: section.dataset.sceneProgress,
      shaderTime: canvas.dataset.previewElapsedSeconds,
      incomingIndex: section.dataset.incomingShaderIndex,
      transitionState: canvas.dataset.previewTransitionState
    };
  })()`);
  assert.equal(pausedState.progress, hiddenRevealState.progress);
  assert.equal(pausedState.shaderTime, stoppedShaderTime);
  assert.equal(pausedState.incomingIndex, "1");
  assert.equal(pausedState.transitionState, "active");

  const returningState = await evaluate(`(async () => {
    const section = document.querySelector('[data-live-shader-banner]');
    const canvas = section.querySelector('[data-shader-canvas="immersive"]');
    const first = Number(canvas.dataset.previewElapsedSeconds);
    window.scrollTo({ top: 0, behavior: 'instant' });
    await new Promise((resolve, reject) => {
      let remainingFrames = 30;
      const observeFrame = () => {
        if (Number(canvas.dataset.previewElapsedSeconds) > first) return resolve();
        remainingFrames -= 1;
        if (remainingFrames === 0) return reject(new Error('The transitioning shader did not resume within 30 frames'));
        requestAnimationFrame(observeFrame);
      };
      requestAnimationFrame(observeFrame);
    });
    const sectionRect = section.getBoundingClientRect();
    const canvasRect = canvas.getBoundingClientRect();
    return {
      bannerVisible: sectionRect.bottom > 0,
      progress: Number(section.dataset.sceneProgress),
      transitionState: canvas.dataset.previewTransitionState,
      canvasMatchesSection: Math.abs(canvasRect.left - sectionRect.left) <= 1
        && Math.abs(canvasRect.top - sectionRect.top) <= 1
        && Math.abs(canvasRect.width - sectionRect.width) <= 1
        && Math.abs(canvasRect.height - sectionRect.height) <= 1,
      backingWidth: canvas.width,
      backingHeight: canvas.height,
      shaderMoves: Number(canvas.dataset.previewElapsedSeconds) > first
    };
  })()`);

  assert.equal(returningState.bannerVisible, true);
  assert.equal(returningState.progress, Number(hiddenRevealState.progress));
  assert.equal(returningState.transitionState, "active");
  assert.equal(returningState.canvasMatchesSection, true);
  assert.equal(returningState.backingWidth, hiddenRevealState.backingWidth);
  assert.equal(returningState.backingHeight, hiddenRevealState.backingHeight);
  assert.equal(returningState.shaderMoves, true);
}
