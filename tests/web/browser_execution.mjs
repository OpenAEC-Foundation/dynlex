import assert from "node:assert/strict";
import {
  captureScreenshot, clickElement, closeBrowserSession, command, dispatchKey, evaluate, findMonacoText,
  hoverMonacoText, navigate, replaceMonacoSource, requestedUrls, runtimeExceptions,
  screenshotDirectory, siteOrigin, sourceEditExpression, waitFor
} from "./browser_test_driver.mjs";
import { verifyOffscreenRevealReturn } from "./shader_visibility_test.mjs";
import {
  assertRiverChallengeLoadingBoundary,
  runRiverChallengeBrowserTest
} from "./river_challenge_browser.mjs";

const shaderManifest = await fetch(`${siteOrigin}/shaders/manifest.json`).then((response) => {
  assert.equal(response.ok, true, "The live shader manifest must load");
  return response.json();
});
const incomingTimeBinding = shaderManifest.scenes[1].uniforms.find(
  (uniform) => uniform.name === "time"
)?.binding;
assert.ok(Number.isInteger(incomingTimeBinding), "The incoming shader must reflect its time uniform");
const nanoTimeBinding = shaderManifest.scenes[2].uniforms.find(
  (uniform) => uniform.name === "time"
)?.binding;
assert.ok(Number.isInteger(nanoTimeBinding), "The volumetric shader must reflect its time uniform");

await navigate("/");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]').length === 2",
  "the runnable homepage sketches"
);
await waitFor("document.fonts.status === 'loaded'", "the homepage fonts");
await waitFor(
  "document.querySelectorAll('.snippet-editor-shell[data-highlight-state=\"cached\"]').length === 4",
  "cached syntax highlighting on every homepage editor"
);
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.shaderPlaylistReady === 'true'",
  "the live shader playlist"
);
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '1'"
    + " && !document.querySelector('[data-shader-next]').disabled",
  "the second shader to preload while the first shader renders"
);
const firstPreloadedShaderState = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const canvas = document.querySelector('[data-layer-state="dormant"] canvas');
  return {
    layer: canvas.parentElement.dataset.shaderLayer,
    revision: Number(canvas.dataset.previewRevision),
    vertexCount: Number(canvas.dataset.previewGeometryVertices),
    horizontalPixels: Number(canvas.dataset.previewGeometryHorizontalPixels),
    sectionPixels: Math.ceil(section.clientWidth * (window.devicePixelRatio || 1))
  };
})()`);
assert.equal(firstPreloadedShaderState.horizontalPixels, firstPreloadedShaderState.sectionPixels);
const shaderState = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const immersiveCanvas = section.querySelector('[data-shader-layer]:not([data-layer-state="dormant"]) canvas');
  const immersiveLayer = immersiveCanvas.parentElement;
  const sectionRect = section.getBoundingClientRect();
  const layerRect = immersiveLayer.getBoundingClientRect();
  const editorLink = new URL(section.querySelector('[data-shader-editor-link]').href);
  return {
    immersiveState: immersiveCanvas.dataset.previewState,
    layerState: immersiveLayer.dataset.layerState,
    immersiveWidth: immersiveCanvas.width,
    immersiveHeight: immersiveCanvas.height,
    immersiveCssWidth: immersiveCanvas.clientWidth,
    immersiveCssHeight: immersiveCanvas.clientHeight,
    pixelRatio: window.devicePixelRatio || 1,
    clipPath: getComputedStyle(immersiveLayer).clipPath,
    filter: getComputedStyle(immersiveLayer).filter,
    sectionHeight: section.getBoundingClientRect().height,
    viewportHeight: window.innerHeight,
    activeShaderIndex: section.dataset.activeShaderIndex,
    scenePhase: section.dataset.scenePhase,
    cloudCoverage: section.dataset.cloudCoverage,
    fillsSection: (
      Math.abs(layerRect.left - sectionRect.left) <= 1
      && Math.abs(layerRect.top - sectionRect.top) <= 1
      && Math.abs(layerRect.width - sectionRect.width) <= 1
      && Math.abs(layerRect.height - sectionRect.height) <= 1
    ),
    laptopOpacity: Number(getComputedStyle(section.querySelector('.shader-laptop')).opacity),
    shaderName: section.querySelector('[data-shader-name]').textContent,
    highlightedTokens: section.querySelectorAll('[data-shader-code] span').length,
    editorMode: editorLink.searchParams.get('mode'),
    editorScene: editorLink.searchParams.get('scene'),
    editorUrlLength: editorLink.href.length
  };
})()`);
assert.equal(shaderState.immersiveState, "ready");
assert.equal(shaderState.layerState, "active");
assert.equal(shaderState.scenePhase, "immersive");
assert.equal(shaderState.cloudCoverage, "viewport");
assert.equal(shaderState.fillsSection, true, "The first shader must fill the banner immediately");
assert.equal(shaderState.clipPath, "none", "The active shader must not retain its reveal mask");
assert.equal(shaderState.filter, "none", "The active shader must not retain its reveal filter");
assert.equal(shaderState.laptopOpacity, 0, "The first shader must not replay the thought-cloud entrance");
assert.ok(
  shaderState.immersiveWidth >= Math.round(shaderState.immersiveCssWidth * shaderState.pixelRatio),
  "The immersive shader must render at the display's physical width"
);
assert.ok(
  shaderState.immersiveHeight >= Math.round(shaderState.immersiveCssHeight * shaderState.pixelRatio),
  "The immersive shader must render at the display's physical height"
);
assert.equal(shaderState.activeShaderIndex, "0");
assert.equal(shaderState.shaderName, shaderManifest.scenes[0].title.toUpperCase());
assert.ok(shaderState.highlightedTokens > 10, "The laptop source must use cached semantic highlighting");
assert.equal(shaderState.editorMode, "shader");
assert.equal(shaderState.editorScene, shaderManifest.scenes[0].id);
assert.ok(shaderState.editorUrlLength < 256, "The editor link must not embed shader source in the request URL");
assert.ok(shaderState.sectionHeight >= shaderState.viewportHeight * 0.9, "The shader must occupy the viewport");
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[0].shaders.fragment.path}`)),
  "The first generated shader must be rendered"
);
assert.equal(
  requestedUrls.some((url) => /\.(?:mp4|webm)(?:$|\?)/i.test(url)),
  false,
  "The live banner must not request encoded media"
);
assert.equal(
  requestedUrls.some((url) => (
    /\.webp(?:$|\?)/i.test(url)
    && !url.includes("/media/river-challenge/")
  )),
  false,
  "Only the visible river challenge preview may request WebP artwork"
);
await captureScreenshot("homepage-initial-immersive");
const immersiveChrome = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const header = document.querySelector('[data-site-header]');
  const headline = section.querySelector('.shader-copy');
  return {
    cloudCoverage: section.dataset.cloudCoverage,
    headerOpacity: getComputedStyle(header).opacity,
    headerVisibility: getComputedStyle(header).visibility,
    headerIsTopLayer: document.elementFromPoint(window.innerWidth / 2, 10)?.closest('[data-site-header]') === header,
    headlineOpacity: Number(getComputedStyle(headline).opacity)
  };
})()`);
assert.equal(immersiveChrome.cloudCoverage, "viewport");
assert.equal(immersiveChrome.headerOpacity, "1");
assert.equal(immersiveChrome.headerVisibility, "visible");
assert.equal(immersiveChrome.headerIsTopLayer, true, "The fixed site header must remain above the immersed shader");
assert.ok(immersiveChrome.headlineOpacity >= 0.78, "The banner headline must remain visible over the shader");
const preparedThought = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  section.querySelector('[data-shader-next]').click();
  if (section.dataset.incomingShaderIndex !== '1') {
    throw new Error('The preloaded shader did not enter its reveal state synchronously');
  }
  const cloudRect = section.querySelector('.thought-assembly').getBoundingClientRect();
  const revealingLayer = section.querySelector('[data-layer-state="revealing"]');
  const revealingRect = revealingLayer.getBoundingClientRect();
  const revealingStyle = getComputedStyle(revealingLayer);
  return {
    layer: revealingLayer.dataset.shaderLayer,
    revision: Number(revealingLayer.querySelector('canvas').dataset.previewRevision),
    clipPath: revealingStyle.clipPath,
    filter: revealingStyle.filter,
    cloudRect: [cloudRect.left, cloudRect.top, cloudRect.width, cloudRect.height],
    revealingRect: [revealingRect.left, revealingRect.top, revealingRect.width, revealingRect.height],
    renderAreaMatchesCloud: (
      Math.abs(revealingRect.left - cloudRect.left) <= 2
      && Math.abs(revealingRect.top - cloudRect.top) <= 2
      && Math.abs(revealingRect.width - cloudRect.width) <= 2
      && Math.abs(revealingRect.height - cloudRect.height) <= 2
    )
  };
})()`);
assert.equal(preparedThought.layer, firstPreloadedShaderState.layer);
assert.equal(
  preparedThought.revision,
  firstPreloadedShaderState.revision,
  "Revealing the next shader must reuse the program compiled while the previous shader rendered"
);
assert.equal(
  preparedThought.renderAreaMatchesCloud,
  true,
  `The shader render surface must begin at the thought-cloud bounds: ${JSON.stringify(preparedThought)}`
);
assert.match(preparedThought.clipPath, /thought-cloud-mask-[01]/);
assert.notEqual(preparedThought.filter, "none", "The revealing thought cloud must retain its glow");
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.incomingShaderIndex === '1'"
    + " && Number(getComputedStyle(document.querySelector('.shader-laptop')).opacity) > 0.75",
  "the next shader code to appear over the outgoing shader"
);
const overlappingThoughts = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const code = section.querySelector('[data-shader-code]');
  const originDot = section.querySelector('[data-thought-origin-dot]');
  const cloudDot = section.querySelector('[data-thought-cloud-dot]');
  const activeLayer = section.querySelector('[data-layer-state="active"]');
  const revealingLayer = section.querySelector('[data-layer-state="revealing"]');
  const codeRect = code.getBoundingClientRect();
  const originRect = originDot.getBoundingClientRect();
  const center = (rect) => ({
    x: rect.left + rect.width / 2,
    y: rect.top + rect.height / 2
  });
  const originCenter = center(originRect);
  const cloudCenter = center(cloudDot.getBoundingClientRect());
  return {
    activeShaderIndex: section.dataset.activeShaderIndex,
    incomingShaderIndex: section.dataset.incomingShaderIndex,
    shaderFile: section.querySelector('[data-shader-file]').textContent,
    codeContainsNextSource: code.textContent === ${JSON.stringify(shaderManifest.scenes[1].source)},
    originInsideCode: (
      originCenter.x >= codeRect.left
      && originCenter.x <= codeRect.right
      && originCenter.y >= codeRect.top
      && originCenter.y <= codeRect.bottom
    ),
    connectorTravelsTowardCloud: cloudCenter.x > originCenter.x && cloudCenter.y < originCenter.y,
    activeLayerState: activeLayer?.dataset.layerState,
    revealingLayerState: revealingLayer?.dataset.layerState,
    revealingLayerIndex: revealingLayer?.dataset.shaderLayer,
    revealingAboveConnector: (
      Number(getComputedStyle(revealingLayer).zIndex)
      > Number(getComputedStyle(originDot.parentElement).zIndex)
    )
  };
})()`);
assert.equal(overlappingThoughts.activeShaderIndex, "0");
assert.equal(overlappingThoughts.incomingShaderIndex, "1");
assert.equal(overlappingThoughts.shaderFile, `${shaderManifest.scenes[1].id}.dl`);
assert.equal(overlappingThoughts.codeContainsNextSource, true);
assert.equal(overlappingThoughts.originInsideCode, true, "The thought connector must begin inside the code");
assert.equal(overlappingThoughts.connectorTravelsTowardCloud, true);
assert.equal(overlappingThoughts.activeLayerState, "active");
assert.equal(overlappingThoughts.revealingLayerState, "revealing");
assert.match(overlappingThoughts.revealingLayerIndex, /^[01]$/);
assert.equal(
  overlappingThoughts.revealingAboveConnector,
  true,
  "The expanding cloud must cover its connector circles"
);
await captureScreenshot("homepage-next-shader-code");
await verifyOffscreenRevealReturn(incomingTimeBinding);
await captureScreenshot("homepage-overlapping-thoughts");
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.scenePhase === 'next-thought'"
    + " && Number(document.querySelector('[data-live-shader-banner]').dataset.sceneProgress) >= 0.89",
  "the incoming thought to expand over the outgoing shader"
);
const expandingViewport = await evaluate(`(() => {
  const sectionRect = document.querySelector('[data-live-shader-banner]').getBoundingClientRect();
  const layerRect = document.querySelector(
    '[data-shader-layer="${overlappingThoughts.revealingLayerIndex}"]'
  ).getBoundingClientRect();
  return {
    reachesTop: Math.abs(layerRect.top - sectionRect.top) <= 2,
    reachesRight: Math.abs(layerRect.right - sectionRect.right) <= 2
  };
})()`);
assert.equal(
  expandingViewport.reachesTop && expandingViewport.reachesRight,
  true,
  "The shader viewport must cover the cloud wherever it reaches the screen edges"
);
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex === '1'",
  "the next live shader"
);
await captureScreenshot("homepage-terrain");
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[1].shaders.fragment.path}`)),
  "Advancing must compile the next configured WebGL program"
);
const editorSceneState = await evaluate(`(() => ({
  activeIndex: Number(document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex),
  incomingIndex: document.querySelector('[data-live-shader-banner]').dataset.incomingShaderIndex,
  shaderFile: document.querySelector('[data-shader-file]').textContent,
  editorScene: new URL(document.querySelector('[data-shader-editor-link]').href).searchParams.get('scene')
}))()`);
const editorSceneIndex = editorSceneState.incomingIndex === undefined
  ? editorSceneState.activeIndex
  : Number(editorSceneState.incomingIndex);
assert.equal(
  editorSceneState.editorScene,
  shaderManifest.scenes[editorSceneIndex].id,
  "The editor action must track the shader displayed in the laptop"
);
assert.equal(editorSceneState.shaderFile, `${editorSceneState.editorScene}.dl`);
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '2'"
    + " || document.querySelector('[data-live-shader-banner]').dataset.incomingShaderIndex === '2'"
    + " || document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex === '2'",
  "the volumetric shader to preload while the terrain shader renders"
);
const nextShaderCanvasState = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const canvas = document.querySelector(
    'canvas[data-preview-geometry-vertices="${shaderManifest.scenes[2].geometry.vertexCount}"]'
  );
  return {
    layer: canvas.parentElement.dataset.shaderLayer,
    revision: Number(canvas.dataset.previewRevision),
    needsAdvance: section.dataset.preloadedShaderIndex === '2'
  };
})()`);
if (nextShaderCanvasState.needsAdvance) {
  await evaluate("document.querySelector('[data-shader-next]').click()");
}
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.activeShaderIndex === '2'",
  "the three-dimensional nano choreography"
);
const thirdShaderCanvasState = await evaluate(`(() => {
  const canvas = document.querySelector('[data-layer-state="active"] canvas');
  return {
    layer: canvas.parentElement.dataset.shaderLayer,
    revision: Number(canvas.dataset.previewRevision),
    state: canvas.dataset.previewState,
    vertexCount: Number(canvas.dataset.previewGeometryVertices)
  };
})()`);
assert.equal(thirdShaderCanvasState.layer, nextShaderCanvasState.layer);
assert.equal(thirdShaderCanvasState.state, "ready");
assert.equal(
  thirdShaderCanvasState.revision,
  nextShaderCanvasState.revision,
  "The preloaded volumetric program must become active without recompiling at code reveal"
);
assert.equal(thirdShaderCanvasState.vertexCount, shaderManifest.scenes[2].geometry.vertexCount);
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[1].shaders.vertex.path}`)),
  "The terrain scene must load its DynLex-compiled displacement vertex shader"
);
const thirdShaderPlaybackState = await evaluate(`(async () => {
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  const section = document.querySelector('[data-live-shader-banner]');
  const canvas = section.querySelector('[data-layer-state="active"] canvas');
  const gl = canvas.getContext('webgl2');
  const timeBuffer = gl.getIndexedParameter(gl.UNIFORM_BUFFER_BINDING, ${nanoTimeBinding});
  if (!timeBuffer) throw new Error('The volumetric shader time buffer is not bound');
  const value = new Float32Array(1);
  gl.bindBuffer(gl.UNIFORM_BUFFER, timeBuffer);
  gl.getBufferSubData(gl.UNIFORM_BUFFER, 0, value);
  return {
    progress: Number(section.dataset.sceneProgress),
    shaderTime: value[0]
  };
})()`);
assert.ok(
  thirdShaderPlaybackState.progress >= 0 && thirdShaderPlaybackState.progress < 1,
  "The promoted shader must remain inside its full-screen banner interval"
);
assert.ok(
  thirdShaderPlaybackState.shaderTime
    - thirdShaderPlaybackState.progress * shaderManifest.scenes[2].durationSeconds
    >= 3.1,
  "The shader animation must continue from the portion already rendered inside its thought cloud"
);
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[2].shaders.vertex.path}`)),
  "The volumetric scene must load its DynLex-compiled vertex shader"
);
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[2].geometry.path}`)),
  "The volumetric scene must load its model-derived point cloud"
);
assert.equal(
  await evaluate(`new URL(document.querySelector('[data-shader-editor-link]').href).searchParams.get('scene')`),
  shaderManifest.scenes[2].id,
  "The editor action must identify the three-dimensional shader scene"
);
assert.equal(
  await evaluate(`new URL(document.querySelector('[data-shader-editor-link]').href).search.length < 256`),
  true,
  "The volumetric editor link must stay below ordinary HTTP request-line limits"
);
const terrainShaderEditorPath = `/ide/index.html?mode=shader&scene=${shaderManifest.scenes[1].id}`;
if (screenshotDirectory) {
  const installFixedNanoFrame = async (elapsedSeconds) => {
    await evaluate(`(async () => {
      let section = document.querySelector('[data-live-shader-banner]');
      if (section.dataset.visualCapture !== 'true') {
        window.__dynlexLiveShaderSection = section;
        const clone = section.cloneNode(true);
        clone.dataset.visualCapture = 'true';
        section.replaceWith(clone);
        section = clone;
      }
      window.__dynlexFixedNanoPreview?.setRunning(false);
      const layer = section.querySelector('[data-layer-state="active"]');
      for (const otherLayer of section.querySelectorAll('.shader-immersion')) {
        if (otherLayer !== layer) otherLayer.remove();
      }
      layer.style.inset = '0';
      layer.style.width = '100%';
      layer.style.height = '100%';
      layer.style.opacity = '1';
      layer.style.clipPath = 'none';
      layer.querySelector('canvas').remove();
      const canvas = document.createElement('canvas');
      canvas.dataset.shaderCanvas = 'immersive';
      layer.prepend(canvas);
      const manifest = await fetch('/shaders/manifest.json').then((response) => response.json());
      const scene = manifest.scenes[2];
      const fragmentSource = await fetch('/' + scene.shaders.fragment.path).then((response) => response.text());
      const vertexSource = await fetch('/' + scene.shaders.vertex.path).then((response) => response.text());
      const geometry = await fetch('/' + scene.geometry.path).then((response) => response.arrayBuffer());
      const { createShaderPreview } = await import('/shader-renderer.js');
      const preview = createShaderPreview(canvas, {
        elapsedSeconds: () => ${elapsedSeconds}
      });
      window.__dynlexFixedNanoPreview = preview;
      preview.replaceProgram({
        fragmentSource,
        vertexSource,
        geometry: { ...scene.geometry, data: geometry }
      }, scene.uniforms);
      section.style.setProperty('--immersion-opacity', '1');
      section.style.setProperty('--laptop-opacity', '0');
      section.style.setProperty('--thought-tail-opacity', '0');
      section.style.setProperty('--shader-copy-opacity', '0.84');
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    })()`);
  };
  await installFixedNanoFrame(3.5);
  await captureScreenshot("homepage-nano-motorcycle");
  await installFixedNanoFrame(4.85);
  await captureScreenshot("homepage-nano-flight-departure");
  await installFixedNanoFrame(5.8);
  await captureScreenshot("homepage-nano-flight-midpoint");
  await installFixedNanoFrame(6.75);
  await captureScreenshot("homepage-nano-flight-landing");
  await installFixedNanoFrame(8.5);
  await captureScreenshot("homepage-nano-vitruvian");
  await installFixedNanoFrame(11.2);
  await captureScreenshot("homepage-nano-measurement-late");
  await installFixedNanoFrame(13);
  await captureScreenshot("homepage-nano-after-cycle");
  await command("Emulation.setDeviceMetricsOverride", {
    width: 1440,
    height: 1000,
    deviceScaleFactor: 2,
    mobile: false
  });
  await installFixedNanoFrame(8.5);
  const highDensityCanvas = await evaluate(`(() => {
    const canvas = document.querySelector('[data-shader-canvas="immersive"]');
    return {
      width: canvas.width,
      cssWidth: canvas.clientWidth,
      pixelRatio: window.devicePixelRatio
    };
  })()`);
  assert.equal(highDensityCanvas.pixelRatio, 2);
  assert.ok(
    highDensityCanvas.width >= highDensityCanvas.cssWidth * 2,
    "The relative drone footprint must be rendered and inspected at high pixel density"
  );
  await captureScreenshot("homepage-nano-vitruvian-hidpi");
  await command("Emulation.clearDeviceMetricsOverride");
  await evaluate(`(() => {
    window.__dynlexFixedNanoPreview.setRunning(false);
    delete window.__dynlexFixedNanoPreview;
    const clone = document.querySelector('[data-live-shader-banner]');
    clone.replaceWith(window.__dynlexLiveShaderSection);
    delete window.__dynlexLiveShaderSection;
  })()`);
}
assert.equal(
  await evaluate("[...document.querySelectorAll('.snippet-editor-shell')].every((shell) => shell.querySelector('.snippet-token'))"),
  true,
  "Every initial snippet must contain compiler-produced token spans"
);
const initialEditorGeometry = await evaluate(`[...document.querySelectorAll('.snippet-editor-shell')].map((shell) => {
  const source = shell.querySelector('[data-snippet-source]');
  const highlight = shell.querySelector('.snippet-highlight');
  return {
    label: source.getAttribute('aria-label'),
    clientHeight: source.clientHeight,
    scrollHeight: source.scrollHeight,
    shellHeight: shell.getBoundingClientRect().height,
    sourceHeight: source.getBoundingClientRect().height,
    highlightHeight: highlight.getBoundingClientRect().height
  };
})`);
assert.deepEqual(
  initialEditorGeometry.filter((editor) => editor.scrollHeight > editor.clientHeight + 1),
  [],
  "Initial snippets must fit before a scrollbar is needed"
);
assert.deepEqual(
  initialEditorGeometry.filter((editor) => (
    Math.abs(editor.shellHeight - editor.sourceHeight) > 1
    || Math.abs(editor.shellHeight - editor.highlightHeight) > 1
  )),
  [],
  "Editable source and syntax overlay must fill their complete frame"
);
assert.equal(
  requestedUrls.some((url) => url.includes("/compiler/")),
  false,
  "The compiler must stay lazy until a visitor edits or runs a sketch"
);
assertRiverChallengeLoadingBoundary(requestedUrls);

const heroEdit = await evaluate(sourceEditExpression(0, `import lib/std.dl

print 81 as a line`));
assert.equal(heroEdit.state, "edited");
assert.match(heroEdit.value, /print 81 as a line/);
await waitFor(
  "document.querySelectorAll('.snippet-editor-shell')[0].dataset.highlightState === 'semantic'",
  "semantic highlighting for the edited hero sketch"
);
assert.ok(
  requestedUrls.some((url) => url.endsWith("/compiler/compiler-worker.js")),
  "Editing must lazily load the shared compiler worker"
);
assert.equal(
  await evaluate(`(() => {
    const shell = document.querySelectorAll('.snippet-editor-shell')[0];
    return shell.querySelector('[data-snippet-source]').value === shell.querySelector('.snippet-highlight').textContent;
  })()`),
  true,
  "Highlighted text must stay synchronized with the editable source"
);
await evaluate("document.querySelectorAll('[data-snippet-run]')[0].click()");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]')[0].dataset.runState === 'done'",
  "the edited hero sketch to run"
);
assert.equal(
  await evaluate("document.querySelectorAll('[data-snippet-output]')[0].textContent.trim()"),
  "81"
);
assert.match(
  await evaluate("document.querySelector('[data-compile-metric]').textContent"),
  /^LAST COMPILE \/ \d+(?:\.\d)? MS$/,
  "The shader readout must report the compiler's measured duration"
);
assert.ok(
  requestedUrls.some((url) => url.endsWith("/compiler/compiler-worker.js")),
  "Running a homepage sketch must load the shared compiler worker"
);

await evaluate("document.querySelector('[data-lab-tab=\"reuse\"]').click()");
await evaluate(sourceEditExpression(1, `import lib/std.dl

print "Words become tools." as a line`));
await evaluate("document.querySelectorAll('[data-snippet-run]')[1].click()");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]')[1].dataset.runState === 'done'",
  "the active language sketch to run"
);
assert.equal(
  await evaluate("document.querySelectorAll('[data-snippet-output]')[1].textContent.trim()"),
  "Words become tools."
);
assert.equal(
  requestedUrls.filter((url) => url.endsWith("/compiler/compiler-worker.js")).length,
  1,
  "Homepage edits and runs must reuse one compiler worker"
);

await runRiverChallengeBrowserTest({
  captureScreenshot,
  clickElement,
  evaluate,
  requestedUrls,
  waitFor
});
await evaluate("document.querySelector('#language').scrollIntoView()");
await new Promise((resolve) => setTimeout(resolve, 1200));
await captureScreenshot("homepage-language");

await navigate(terrainShaderEditorPath);
await waitFor(
  "document.querySelector('#shader-preview')?.dataset.previewState === 'ready'",
  "the editable shader's first successful preview"
);
await waitFor(
  `(() => {
    const lines = document.querySelector('.view-lines');
    if (!lines) return false;
    const defaultColor = getComputedStyle(lines).color;
    return new Set(
      [...lines.querySelectorAll('span')]
        .map((node) => getComputedStyle(node).color)
        .filter((color) => color !== defaultColor)
    ).size >= 3;
  })()`,
  "the shader LSP semantic tokens to render",
  10000
);
const initialShaderState = await evaluate(`(() => {
  const canvas = document.querySelector('#shader-preview');
  const shell = document.querySelector('#shader-preview-shell');
  const toolPanel = document.querySelector('.tool-panel');
  const slider = document.querySelector('.monaco-scrollable-element > .scrollbar.vertical > .slider');
  const defaultColor = getComputedStyle(document.querySelector('.view-lines')).color;
  const tokenColors = new Set(
    [...document.querySelectorAll('.view-lines span')]
      .map((node) => getComputedStyle(node).color)
      .filter((color) => color !== defaultColor)
  );
  return {
    mode: document.documentElement.dataset.workspaceMode,
    revision: Number(canvas.dataset.previewRevision),
    vertexCount: Number(canvas.dataset.previewGeometryVertices),
    horizontalPixels: Number(canvas.dataset.previewGeometryHorizontalPixels),
    canvasPixels: Math.ceil(canvas.clientWidth * (window.devicePixelRatio || 1)),
    canvasHeight: canvas.getBoundingClientRect().height,
    shellHeight: shell.getBoundingClientRect().height,
    toolPanelHeight: toolPanel.getBoundingClientRect().height,
    scrollbarColor: slider ? getComputedStyle(slider).backgroundColor : '',
    tokenColorCount: tokenColors.size
  };
})()`);
assert.equal(initialShaderState.mode, "shader");
assert.ok(initialShaderState.revision >= 1);
assert.ok(initialShaderState.vertexCount > 0);
assert.equal(initialShaderState.horizontalPixels, initialShaderState.canvasPixels);
assert.ok(
  initialShaderState.canvasHeight >= initialShaderState.toolPanelHeight * 0.75
    && Math.abs(initialShaderState.canvasHeight - initialShaderState.shellHeight) <= 1,
  `The live shader must fill the available preview frame: ${JSON.stringify(initialShaderState)}`
);
assert.notEqual(initialShaderState.scrollbarColor, "rgb(255, 255, 255)", "The IDE scrollbar must not be white");
assert.ok(initialShaderState.tokenColorCount >= 3, "The shader source must be syntax highlighted");
await findMonacoText("fold");
await hoverMonacoText("fold");
await waitFor(
  "[...document.querySelectorAll('.monaco-hover:not(.hidden)')].some((hover) => hover.textContent.trim().length > 0)",
  "the shader IDE to display DynLex hover information",
  10000
);
await captureScreenshot("ide-shader-initial");

await replaceMonacoSource(`import lib/shader.dl

if this is a vertex shader:
    set the output position with x the vertex x, y the vertex y, z the vertex z and w 1.0
else:
    set pulse to the shader time
    set pulse to the sine of pulse
    set pulse to pulse * 0.5 + 0.5
    set glow to the shader render pass * 0.22
    set the fragment color with a red channel of (pulse + glow), a green channel of 0.12, a blue channel of 0.72 and an alpha channel of 1.0
`);
await waitFor(
  `Number(document.querySelector('#shader-preview').dataset.previewRevision) > ${initialShaderState.revision}`,
  "a valid edit to replace the live shader"
);
const successfulShaderRevision = await evaluate("Number(document.querySelector('#shader-preview').dataset.previewRevision)");
await captureScreenshot("ide-shader-valid");

await replaceMonacoSource("this shader does not compile");
await waitFor(
  "document.querySelector('#status-text')?.textContent.includes('problem')",
  "an invalid shader edit to report a problem"
);
assert.equal(
  await evaluate("Number(document.querySelector('#shader-preview').dataset.previewRevision)"),
  successfulShaderRevision,
  "A failed recompilation must keep the last successful shader live"
);
await captureScreenshot("ide-shader-live");

const invalidSource = encodeURIComponent("this phrase does not exist");
await navigate(`/ide/?code=${invalidSource}`);
await waitFor(
  "document.querySelector('#status-text')?.textContent.includes('problem')",
  "the IDE to display an invalid-source diagnostic"
);
assert.notEqual(await evaluate("document.querySelector('#diagnostics-count').textContent"), "0");

await navigate("/ide/?autorun=1");
await waitFor(
  "document.querySelector('#status-text')?.textContent === 'Finished'",
  "the IDE to compile and run its starting sketch"
);
assert.equal(await evaluate("document.querySelector('#runtime-output').textContent.trim()"), "64");
await hoverMonacoText("square", 1);
await waitFor(
  `[...document.querySelectorAll('.monaco-hover:not(.hidden)')].some((hover) => {
    const rect = hover.getBoundingClientRect();
    const style = getComputedStyle(hover);
    const topmost = document.elementFromPoint(
      Math.max(0, Math.min(innerWidth - 1, rect.left + rect.width / 2)),
      Math.max(0, Math.min(innerHeight - 1, rect.top + rect.height / 2))
    );
    return (
      hover.textContent.trim().length > 0
      && rect.width > 0
      && rect.height > 0
      && rect.right > 0
      && rect.bottom > 0
      && rect.left < innerWidth
      && rect.top < innerHeight
      && style.visibility === 'visible'
      && style.opacity !== '0'
      && topmost
      && hover.contains(topmost)
    );
  })`,
  "hovering a DynLex token to display language-server information",
  10000
);
await captureScreenshot("ide-hover");
await hoverMonacoText("square");
await waitFor(`[...document.querySelectorAll('.monaco-hover:not(.hidden)')].some((hover) => hover.textContent.includes('Choose inferred instance') && hover.textContent.includes('{a 32-bit integer:value} squared'))`, "the inferred-instance hover to display its parameter type");
await findMonacoText("squared", 1);
await waitFor(
  "document.querySelector('.line-numbers.active-line-number')?.textContent.trim() === '7'",
  "the square invocation to be selected"
);
await dispatchKey("F12", "F12", 123);
await waitFor(
  "document.querySelector('.line-numbers.active-line-number')?.textContent.trim() === '3'",
  "F12 to navigate from the square invocation to its definition"
);
await findMonacoText("print 8 squared");
await dispatchKey("F12", "F12", 123);
await waitFor(
  "[...document.querySelectorAll('[data-current-file]')].every((label) => label.textContent === 'string.dl')",
  "F12 to open the imported print definition"
);
await waitFor(
  `(() => {
    const defaultColor = getComputedStyle(document.querySelector('.view-lines')).color;
    return new Set(
      [...document.querySelectorAll('.view-lines span')]
        .map((node) => getComputedStyle(node).color)
        .filter((color) => color !== defaultColor)
    ).size >= 3;
  })()`,
  "the imported definition to receive DynLex semantic highlighting",
  10000
);
assert.deepEqual(
  await evaluate("[...document.querySelectorAll('[data-file-uri]')].map((file) => file.textContent.trim())"),
  ["main.dl", "string.dl"],
  "Definition navigation must retain both the editable file and the opened library file"
);
await command("Input.insertText", { text: "SHOULD_NOT_EDIT" });
await new Promise((resolve) => setTimeout(resolve, 250));
assert.equal(
  await evaluate("document.querySelector('.view-lines').textContent.includes('SHOULD_NOT_EDIT')"),
  false,
  "Imported library definitions must reject edits"
);
await evaluate("document.querySelector('[data-file-uri=\"file:///workspace/main.dl\"]').click()");
await waitFor(
  "[...document.querySelectorAll('[data-current-file]')].every((label) => label.textContent === 'main.dl')",
  "the editable file to reopen from the project list"
);
await waitFor(
  "document.querySelector('.view-lines').textContent.replace(/\\u00a0/g, ' ').includes('print 8 squared as a line')",
  "returning from a definition to render the preserved editable source model"
);
await captureScreenshot("ide-finished");
await command("Emulation.setDeviceMetricsOverride", {
  width: 2560,
  height: 900,
  deviceScaleFactor: 1,
  mobile: false
});
await navigate("/");
await waitFor("document.querySelector('[data-live-shader-banner]')?.dataset.shaderPlaylistReady === 'true'", "the ultrawide shader banner");
await waitFor("document.querySelector('[data-live-shader-banner]').dataset.preloadedShaderIndex === '1'", "the ultrawide terrain topology");
const ultrawideThoughtLayout = await evaluate(`(() => {
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
assert.equal(ultrawideThoughtLayout.terrainPixels, ultrawideThoughtLayout.sectionPixels);
assert.ok(ultrawideThoughtLayout.terrainVertices > firstPreloadedShaderState.vertexCount);
assert.ok(
  ultrawideThoughtLayout.cloudWidthInScreens >= 0.74
    && ultrawideThoughtLayout.cloudWidthInScreens <= 0.82,
  "The thought cloud must scale from the code screen"
);
assert.ok(
  ultrawideThoughtLayout.gapInScreens >= 0.02
    && ultrawideThoughtLayout.gapInScreens <= 0.06,
  "The thought cloud must stay attached to the code screen on ultrawide displays"
);
assert.equal(
  ultrawideThoughtLayout.guideIsTransparent,
  true,
  "Only the live shader layer may paint the thought cloud"
);
if (screenshotDirectory) {
  await captureScreenshot("homepage-ultrawide");
}
await command("Emulation.clearDeviceMetricsOverride");

await command("Emulation.setDeviceMetricsOverride", {
  width: 390,
  height: 844,
  deviceScaleFactor: 1,
  mobile: true
});
await navigate("/");
await waitFor("document.querySelector('[data-runnable-sketch]')", "the mobile homepage");
await waitFor("document.fonts.status === 'loaded'", "the mobile homepage fonts");
await waitFor(
  "[...document.querySelectorAll('[data-snippet-source]')].every((source) => source.closest('.snippet-editor-shell'))",
  "the mobile snippet editors"
);
const mobileLayout = await evaluate(`({
  viewportWidth: window.innerWidth,
  pageWidth: document.documentElement.scrollWidth,
  overflowingEditors: [...document.querySelectorAll('[data-snippet-source]')]
    .filter((source) => source.scrollHeight > source.clientHeight + 1)
    .map((source) => {
      const shell = source.closest('.snippet-editor-shell');
      return {
        label: source.getAttribute('aria-label'),
        clientHeight: source.clientHeight,
        scrollHeight: source.scrollHeight,
        computedHeight: getComputedStyle(source).height,
        lineHeight: getComputedStyle(source).lineHeight,
        padding: getComputedStyle(source).padding,
        shellClass: shell.className,
        shellHeight: shell.getBoundingClientRect().height,
        shellComputedHeight: getComputedStyle(shell).height,
        configuredHeight: getComputedStyle(shell).getPropertyValue('--snippet-frame-height')
      };
    }),
  runButtonsFit: [...document.querySelectorAll('[data-snippet-run]')].every((button) => {
    const bounds = button.getBoundingClientRect();
    return bounds.left >= 0 && bounds.right <= window.innerWidth;
  })
})`);
assert.equal(mobileLayout.pageWidth, mobileLayout.viewportWidth, "Mobile homepage must not scroll sideways");
assert.deepEqual(mobileLayout.overflowingEditors, [], "Initial mobile snippets must fit before scrolling");
assert.equal(mobileLayout.runButtonsFit, true, "Mobile run controls must remain inside the viewport");
await clickElement("[data-river-challenge-load]");
await waitFor(
  "document.querySelector('[data-river-challenge-mount]').dataset.challengeLoaded === 'true'",
  "the mobile river challenge"
);
const mobileChallengeLayout = await evaluate(`(() => ({
  viewportWidth: window.innerWidth,
  pageWidth: document.documentElement.scrollWidth,
  controlsFit: [...document.querySelectorAll('.river-playback button')].every((button) => {
    const bounds = button.getBoundingClientRect();
    return bounds.left >= 0 && bounds.right <= window.innerWidth;
  }),
  editorFits: document.querySelector('[data-river-editor-shell]').scrollWidth
    <= document.querySelector('[data-river-editor-shell]').clientWidth
}))()`);
assert.equal(
  mobileChallengeLayout.pageWidth,
  mobileChallengeLayout.viewportWidth,
  "The opened mobile challenge must not scroll sideways"
);
assert.equal(mobileChallengeLayout.controlsFit, true, "Mobile challenge controls must fit the viewport");
assert.equal(mobileChallengeLayout.editorFits, true, "The mobile challenge editor must fit its panel");
await captureScreenshot("homepage-challenge-mobile");
await evaluate("window.scrollTo({ top: 0 })");
await evaluate("document.querySelector('.menu-toggle').click()");
await waitFor("document.body.classList.contains('menu-open')", "the opaque mobile navigation to open");
const mobileMenu = await evaluate(`(() => {
  const header = document.querySelector('[data-site-header]');
  const nav = document.querySelector('[data-primary-nav]');
  const firstLink = nav.querySelector('a');
  const headerRect = header.getBoundingClientRect();
  const navRect = nav.getBoundingClientRect();
  const firstLinkRect = firstLink.getBoundingClientRect();
  return {
    background: getComputedStyle(nav).backgroundColor,
    headerHeight: headerRect.height,
    navTop: navRect.top,
    firstLinkTop: firstLinkRect.top,
    viewportHeight: window.innerHeight,
    itemsInside: [...nav.querySelectorAll('a')].every((link) => {
      const bounds = link.getBoundingClientRect();
      return bounds.top >= navRect.top && bounds.bottom <= navRect.bottom;
    })
  };
})()`);
assert.equal(mobileMenu.background, "rgb(8, 10, 9)", "The open menu must be fully opaque");
assert.ok(mobileMenu.headerHeight >= mobileMenu.viewportHeight, "The open header must cover the viewport");
assert.ok(mobileMenu.navTop >= 65, "Menu items must start below the mobile header");
assert.ok(mobileMenu.firstLinkTop >= mobileMenu.navTop + 20, "Menu items need breathing room below the header");
assert.equal(mobileMenu.itemsInside, true, "Every mobile navigation item must remain inside the menu");
await captureScreenshot("homepage-mobile");
await navigate("/wiki/sections/function.html");
await waitFor("document.querySelector('[data-primary-nav] a')", "the documentation navigation");
await evaluate("document.querySelector('.menu-toggle').click()");
await waitFor("document.body.classList.contains('menu-open')", "the documentation menu to open");
const documentationMenu = await evaluate(`(() => {
  const nav = document.querySelector('[data-primary-nav]');
  return {
    background: getComputedStyle(nav).backgroundColor,
    current: nav.querySelector('[aria-current="page"]')?.textContent.trim(),
    paths: [...nav.querySelectorAll('a')].map((link) => {
      const url = new URL(link.href);
      return url.pathname + url.hash;
    }),
    styledCardBackground: getComputedStyle(document.querySelector('.hero-card')).backgroundColor
  };
})()`);
assert.equal(documentationMenu.background, "rgb(8, 10, 9)");
assert.equal(documentationMenu.current, "Docs");
assert.deepEqual(
  documentationMenu.paths,
  ["/index.html#challenges", "/index.html#language", "/index.html#studio", "/wiki/index.html", "/ide/index.html"]
);
assert.equal(
  documentationMenu.styledCardBackground,
  "rgb(13, 16, 14)",
  "Documentation must use the homepage surface palette"
);
await evaluate("document.querySelector('[data-primary-nav] a[aria-current=\"page\"]').click()");
await waitFor("window.location.pathname === '/wiki/index.html'", "documentation navigation to reach the docs home");
await captureScreenshot("documentation-mobile");
await command("Emulation.clearDeviceMetricsOverride");
assert.deepEqual(
  runtimeExceptions,
  [],
  "Homepage and IDE interactions must not raise uncaught browser exceptions"
);

closeBrowserSession();
console.log("Homepage challenge, examples, and IDE compile and run in Chrome.");
