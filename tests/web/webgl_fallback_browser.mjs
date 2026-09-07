import assert from "node:assert/strict";
import {
  captureScreenshot,
  command,
  evaluate,
  navigate,
  requestedUrls,
  waitFor
} from "./browser_test_driver.mjs";

export async function verifyWebGlFallback() {
  const injection = await command("Page.addScriptToEvaluateOnNewDocument", {
    source: "Object.defineProperty(Navigator.prototype, 'gpu', { configurable: true, get: () => undefined });"
  });
  try {
    await navigate("/");
    await waitFor(
      "document.querySelector('[data-live-shader-banner]')?.dataset.shaderPlaylistReady === 'true'",
      "the WebGL shader playlist"
    );
    await waitFor(
      `document.querySelector('[data-shader-canvas="immersive"]').dataset.previewApi === 'webgl2'`,
      "the WebGL2 homepage renderer"
    );
    await waitFor(
      `Number(document.querySelector('[data-shader-canvas="immersive"]').dataset.previewElapsedSeconds) > 0`,
      "the first WebGL shader frame"
    );
    assert.equal(
      await evaluate(`document.querySelectorAll('[data-shader-canvas="immersive"]').length`),
      1
    );
    await captureScreenshot("homepage-webgl");
    await waitFor(
      "document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '1'",
      "the WebGL terrain preload"
    );
    await evaluate("document.querySelector('[data-shader-next]').click()");
    await waitFor(
      `document.querySelector('[data-shader-canvas="immersive"]').dataset.previewTransitionState === 'active'`,
      "the WebGL stencil transition"
    );
    await waitFor(
      "document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex === '1'",
      "the WebGL terrain promotion"
    );
    await waitFor(
      "document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '2'",
      "the WebGL nano choreography preload"
    );
    await evaluate("document.querySelector('[data-shader-next]').click()");
    await waitFor(
      "document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex === '2'",
      "the WebGL nano choreography promotion"
    );
    assert.ok(
      await evaluate(
        "Number(document.querySelector('[data-shader-canvas=\"immersive\"]').dataset.previewGeometryVertices) > 0"
      ),
      "The WebGL nano choreography must render its geometry"
    );
    assert.ok(
      requestedUrls.some((url) => url.endsWith(".glsl")),
      "The WebGL homepage must request generated GLSL"
    );
    assert.equal(
      requestedUrls.some((url) => url.endsWith(".wgsl")),
      false,
      "The WebGL homepage must not download WebGPU shader sources"
    );
    await navigate("/ide/index.html?mode=shader&scene=endless-terrain");
    await waitFor(
      "document.querySelector('#shader-preview')?.dataset.previewState === 'ready'",
      "the WebGL IDE shader preview"
    );
    assert.equal(
      await evaluate("document.querySelector('#shader-preview').dataset.previewApi"),
      "webgl2"
    );
    assert.ok(
      await evaluate("Number(document.querySelector('#shader-preview').dataset.previewGeometryVertices)") > 0
    );
  } finally {
    await command("Page.removeScriptToEvaluateOnNewDocument", {
      identifier: injection.identifier
    });
  }
}
