import assert from "node:assert/strict";
import {
  captureScreenshot, command, evaluate, navigate, screenshotDirectory, waitFor
} from "./browser_test_driver.mjs";

export async function verifyHomepageUltrawideLayout(firstPreloadedVertexCount) {
  await command("Emulation.setDeviceMetricsOverride", {
    width: 2560,
    height: 900,
    deviceScaleFactor: 1,
    mobile: false
  });
  await navigate("/");
  await waitFor(
    "document.querySelector('[data-live-shader-banner]')?.dataset.shaderPlaylistReady === 'true'",
    "the ultrawide shader banner"
  );
  await waitFor(
    "document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '1'",
    "the ultrawide terrain topology"
  );
  const layout = await evaluate(`(() => {
    const section = document.querySelector('[data-live-shader-banner]');
    const codeRect = document.querySelector('[data-shader-code]').getBoundingClientRect();
    const cloudGuide = document.querySelector('.thought-assembly');
    const cloudRect = cloudGuide.getBoundingClientRect();
    const terrainCanvas = section.querySelector('[data-layer-state="dormant"] canvas');
    return {
      cloudWidthInScreens: cloudRect.width / codeRect.width,
      gapInScreens: (cloudRect.left - codeRect.right) / codeRect.width,
      terrainVertices: Number(terrainCanvas.dataset.previewGeometryVertices),
      terrainPixels: Number(terrainCanvas.dataset.previewGeometryHorizontalPixels),
      sectionPixels: Math.ceil(section.clientWidth * (window.devicePixelRatio || 1)),
      guideIsTransparent: (
        getComputedStyle(cloudGuide).backgroundImage === 'none'
        && getComputedStyle(cloudGuide).backgroundColor === 'rgba(0, 0, 0, 0)'
        && getComputedStyle(cloudGuide).clipPath === 'none'
      )
    };
  })()`);
  assert.equal(layout.terrainPixels, layout.sectionPixels);
  assert.ok(layout.terrainVertices > firstPreloadedVertexCount);
  assert.ok(
    layout.cloudWidthInScreens >= 0.74 && layout.cloudWidthInScreens <= 0.82,
    "The thought cloud must scale from the code screen"
  );
  assert.ok(
    layout.gapInScreens >= 0.02 && layout.gapInScreens <= 0.06,
    "The thought cloud must stay attached to the code screen on ultrawide displays"
  );
  assert.equal(
    layout.guideIsTransparent,
    true,
    "Only the live shader layer may paint the thought cloud"
  );
  if (screenshotDirectory) {
    await captureScreenshot("homepage-ultrawide");
  }
  await command("Emulation.clearDeviceMetricsOverride");
}
