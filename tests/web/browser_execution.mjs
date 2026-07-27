import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

const cdpOrigin = process.env.DYNLEX_CDP_ORIGIN || "http://127.0.0.1:9222";
const siteOrigin = process.env.DYNLEX_SITE_ORIGIN || "http://127.0.0.1:8765";
const screenshotDirectory = process.env.DYNLEX_SCREENSHOT_DIRECTORY;
const shaderManifest = await fetch(`${siteOrigin}/shaders/manifest.json`).then((response) => {
  assert.equal(response.ok, true, "The live shader manifest must load");
  return response.json();
});
const incomingTimeBinding = shaderManifest.scenes[1].uniforms.find(
  (uniform) => uniform.name === "time"
)?.binding;
assert.ok(Number.isInteger(incomingTimeBinding), "The incoming shader must reflect its time uniform");

const targets = await fetch(`${cdpOrigin}/json/list`).then((response) => response.json());
const pageTarget = targets.find((target) => target.type === "page");
assert.ok(pageTarget?.webSocketDebuggerUrl, "Chrome must expose a page target over CDP");

const socket = new WebSocket(pageTarget.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});

let nextCommandId = 1;
const pendingCommands = new Map();
const requestedUrls = [];
const runtimeExceptions = [];
const consoleMessages = [];

socket.addEventListener("message", (event) => {
  const message = JSON.parse(event.data);
  if (typeof message.id === "number") {
    const pending = pendingCommands.get(message.id);
    if (!pending) return;
    pendingCommands.delete(message.id);
    if (message.error) {
      pending.reject(new Error(message.error.message));
    } else {
      pending.resolve(message.result);
    }
    return;
  }

  if (message.method === "Network.requestWillBeSent") {
    requestedUrls.push(message.params.request.url);
  } else if (message.method === "Runtime.exceptionThrown") {
    runtimeExceptions.push(message.params.exceptionDetails);
  } else if (message.method === "Runtime.consoleAPICalled") {
    consoleMessages.push({
      type: message.params.type,
      values: message.params.args.map((argument) => argument.value ?? argument.description ?? argument.type)
    });
  }
});

function command(method, params = {}) {
  const id = nextCommandId++;
  return new Promise((resolve, reject) => {
    pendingCommands.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
  });
}

async function evaluate(expression) {
  const response = await command("Runtime.evaluate", {
    expression,
    awaitPromise: true,
    returnByValue: true
  });
  if (response.exceptionDetails) {
    throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text);
  }
  return response.result.value;
}

async function waitFor(expression, description, timeoutMilliseconds = 120000) {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    if (await evaluate(`Boolean(${expression})`)) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  const pageState = await evaluate(`(() => ({
    status: document.querySelector('#status-text')?.textContent ?? '',
    diagnostics: document.querySelector('#diagnostics-list')?.textContent ?? '',
    activity: document.querySelector('#compiler-log')?.textContent ?? '',
    source: document.querySelector('.monaco-editor')?.textContent ?? ''
  }))()`);
  throw new Error(
    `Timed out waiting for ${description}\n`
      + `Page state: ${JSON.stringify(pageState)}\n`
      + `Console: ${JSON.stringify(consoleMessages.slice(-10))}\n`
      + `Runtime exceptions: ${JSON.stringify(runtimeExceptions.slice(-10))}`
  );
}

async function navigate(path) {
  await command("Page.navigate", { url: `${siteOrigin}${path}` });
  await waitFor("document.readyState === 'complete'", `${path} to load`);
}

async function captureScreenshot(name) {
  if (!screenshotDirectory) return;
  fs.mkdirSync(screenshotDirectory, { recursive: true });
  const response = await command("Page.captureScreenshot", { format: "png" });
  fs.writeFileSync(path.join(screenshotDirectory, `${name}.png`), response.data, "base64");
}

async function dispatchKey(key, code, virtualKeyCode, modifiers = 0) {
  for (const type of ["keyDown", "keyUp"]) {
    await command("Input.dispatchKeyEvent", {
      type,
      key,
      code,
      windowsVirtualKeyCode: virtualKeyCode,
      nativeVirtualKeyCode: virtualKeyCode,
      modifiers
    });
  }
}

function sourceEditExpression(sketchIndex, sourceText) {
  return `(() => {
    const sketch = document.querySelectorAll('[data-runnable-sketch]')[${sketchIndex}];
    const source = sketch.querySelector('[data-lab-panel]:not([hidden]) [data-snippet-source]')
      || sketch.querySelector('[data-snippet-source]');
    source.value = ${JSON.stringify(sourceText)};
    source.dispatchEvent(new Event('input', { bubbles: true }));
    return { state: sketch.dataset.runState, value: source.value };
  })()`;
}

async function replaceMonacoSource(sourceText) {
  await command("Page.bringToFront");
  await evaluate(`(() => {
    const input = document.querySelector('.monaco-editor textarea.inputarea');
    if (!input) throw new Error('Monaco input is missing');
    input.focus();
  })()`);
  await evaluate(`navigator.clipboard.writeText(${JSON.stringify(sourceText)})`);
  await dispatchKey("a", "KeyA", 65, 2);
  await dispatchKey("v", "KeyV", 86, 2);
}

async function findMonacoText(text) {
  await evaluate(`(() => {
    const input = document.querySelector('.monaco-editor textarea.inputarea');
    if (!input) throw new Error('Monaco input is missing');
    input.focus();
  })()`);
  await dispatchKey("f", "KeyF", 70, 2);
  await dispatchKey("a", "KeyA", 65, 2);
  await command("Input.insertText", { text });
  await dispatchKey("Enter", "Enter", 13);
  await dispatchKey("Escape", "Escape", 27);
  await dispatchKey("ArrowLeft", "ArrowLeft", 37);
}

async function hoverMonacoText(text, occurrence = 0) {
  await command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 0, y: 0 });
  await new Promise((resolve) => setTimeout(resolve, 400));
  const point = await evaluate(`(() => {
    const walker = document.createTreeWalker(
      document.querySelector('.view-lines'),
      NodeFilter.SHOW_TEXT
    );
    let remaining = ${occurrence};
    while (walker.nextNode()) {
      const index = walker.currentNode.data.indexOf(${JSON.stringify(text)});
      if (index === -1) continue;
      if (remaining > 0) {
        remaining -= 1;
        continue;
      }
      const range = document.createRange();
      range.setStart(walker.currentNode, index);
      range.setEnd(walker.currentNode, index + ${JSON.stringify(text)}.length);
      const rect = range.getBoundingClientRect();
      return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    }
    throw new Error(${JSON.stringify(`Monaco text is not visible: ${text}`)});
  })()`);
  await command("Input.dispatchMouseEvent", {
    type: "mouseMoved",
    x: point.x,
    y: point.y
  });
}

await command("Page.enable");
await command("Runtime.enable");
await command("Network.enable");
await command("Browser.grantPermissions", {
  origin: siteOrigin,
  permissions: ["clipboardReadWrite", "clipboardSanitizedWrite"]
});

await navigate("/");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]').length === 5",
  "the runnable homepage sketches"
);
await waitFor("document.fonts.status === 'loaded'", "the homepage fonts");
await waitFor(
  "document.querySelectorAll('.snippet-editor-shell[data-highlight-state=\"cached\"]').length === 7",
  "cached syntax highlighting on every homepage editor"
);
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.shaderPlaylistReady === 'true'",
  "the live shader playlist"
);
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
  requestedUrls.some((url) => /\.(?:mp4|webm|webp)(?:$|\?)/i.test(url)),
  false,
  "The live banner must not request encoded media"
);
await captureScreenshot("homepage-initial-immersive");
const immersiveChrome = await evaluate(`(() => {
  const section = document.querySelector('[data-live-shader-banner]');
  const header = document.querySelector('[data-site-header]');
  const headline = section.querySelector('.shader-copy');
  const path = section.querySelector('[data-thought-cloud-path]');
  const geometry = section.querySelector('#thought-cloud-geometry');
  const coveredCorners = [
    [1 / 3, 1 / 3],
    [2 / 3, 1 / 3],
    [1 / 3, 2 / 3],
    [2 / 3, 2 / 3]
  ].every(([x, y]) => geometry.isPointInFill(new DOMPoint(x, y)));
  return {
    cloudTransform: path.getAttribute('transform'),
    cloudCoverage: section.dataset.cloudCoverage,
    headerOpacity: getComputedStyle(header).opacity,
    headerVisibility: getComputedStyle(header).visibility,
    headerIsTopLayer: document.elementFromPoint(window.innerWidth / 2, 10)?.closest('[data-site-header]') === header,
    headlineOpacity: Number(getComputedStyle(headline).opacity),
    coveredCorners
  };
})()`);
assert.equal(immersiveChrome.cloudTransform, "translate(-1 -1) scale(3 3)");
assert.equal(immersiveChrome.cloudCoverage, "viewport");
assert.equal(immersiveChrome.coveredCorners, true, "Every viewport corner must be inside the expanded cloud");
assert.equal(immersiveChrome.headerOpacity, "1");
assert.equal(immersiveChrome.headerVisibility, "visible");
assert.equal(immersiveChrome.headerIsTopLayer, true, "The fixed site header must remain above the immersed shader");
assert.ok(immersiveChrome.headlineOpacity >= 0.78, "The banner headline must remain visible over the shader");
await evaluate("document.querySelector('[data-shader-next]').click()");
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
  const cloudGuide = section.querySelector('.thought-assembly');
  const activeLayer = section.querySelector('[data-layer-state="active"]');
  const revealingLayer = section.querySelector('[data-layer-state="revealing"]');
  const codeRect = code.getBoundingClientRect();
  const cloudRect = cloudGuide.getBoundingClientRect();
  const revealingRect = revealingLayer.getBoundingClientRect();
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
    renderAreaMatchesCloud: (
      Math.abs(revealingRect.left - cloudRect.left) <= 2
      && Math.abs(revealingRect.top - cloudRect.top) <= 2
      && Math.abs(revealingRect.width - cloudRect.width) <= 2
      && Math.abs(revealingRect.height - cloudRect.height) <= 2
    ),
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
  overlappingThoughts.renderAreaMatchesCloud,
  true,
  "The shader render surface must begin at the thought-cloud bounds"
);
assert.equal(
  overlappingThoughts.revealingAboveConnector,
  true,
  "The expanding cloud must cover its connector circles"
);
await captureScreenshot("homepage-next-shader-code");
await waitFor(
  "document.querySelector('[data-live-shader-banner]').dataset.scenePhase === 'next-thought'"
    + " && Number(document.querySelector('[data-live-shader-banner]').dataset.sceneProgress) >= 0.8",
  "the next shader to appear inside its thought cloud"
);
const incomingShaderMoves = await evaluate(`(async () => {
  const canvas = document.querySelector(
    '[data-shader-layer="${overlappingThoughts.revealingLayerIndex}"] canvas'
  );
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
  await new Promise((resolve) => {
    let frameCount = 0;
    const waitForRenderedFrame = () => {
      frameCount += 1;
      if (frameCount === 4) {
        resolve();
      } else {
        requestAnimationFrame(waitForRenderedFrame);
      }
    };
    requestAnimationFrame(waitForRenderedFrame);
  });
  return readTime() > first;
})()`);
assert.equal(incomingShaderMoves, true, "The shader must animate while it is inside the thought cloud");
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
assert.ok(
  requestedUrls.some((url) => url.endsWith(`/${shaderManifest.scenes[1].shaders.fragment.path}`)),
  "Advancing must compile the next configured WebGL program"
);
assert.equal(
  await evaluate(`new URL(document.querySelector('[data-shader-editor-link]').href).searchParams.get('scene')`),
  shaderManifest.scenes[1].id,
  "The editor action must track the visible shader"
);
const nextShaderCanvasState = await evaluate(`(() => {
  const canvas = document.querySelector('[data-layer-state="dormant"] canvas');
  return {
    layer: canvas.parentElement.dataset.shaderLayer,
    revision: Number(canvas.dataset.previewRevision)
  };
})()`);
await evaluate("document.querySelector('[data-shader-next]').click()");
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
    pointCount: Number(canvas.dataset.previewGeometryPoints)
  };
})()`);
assert.equal(thirdShaderCanvasState.layer, nextShaderCanvasState.layer);
assert.equal(thirdShaderCanvasState.state, "ready");
assert.ok(
  thirdShaderCanvasState.revision > nextShaderCanvasState.revision,
  "The third generated shader must compile and replace the active WebGL program"
);
assert.equal(thirdShaderCanvasState.pointCount, shaderManifest.scenes[2].geometry.pointCount);
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
const volumetricShaderEditorPath = await evaluate(`(() => {
  const url = new URL(document.querySelector('[data-shader-editor-link]').href);
  return url.pathname + url.search;
})()`);
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
  await installFixedNanoFrame(6.5);
  await captureScreenshot("homepage-nano-motorcycle");
  await installFixedNanoFrame(9.5);
  await captureScreenshot("homepage-nano-vitruvian");
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

const heroEdit = await evaluate(sourceEditExpression(0, `import lib/std.dl

print 81 as line`));
assert.equal(heroEdit.state, "edited");
assert.match(heroEdit.value, /print 81 as line/);
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

await evaluate(sourceEditExpression(1, `import lib/std.dl

loop 2 times:
    print "Hello" as line`));
await evaluate(sourceEditExpression(2, `import lib/std.dl

print "neon violet" as line`));
await evaluate(`(() => {
  const buttons = document.querySelectorAll('[data-snippet-run]');
  buttons[1].click();
  buttons[2].click();
})()`);
await waitFor(
  "[...document.querySelectorAll('[data-runnable-sketch]')].slice(1, 3).every((sketch) => sketch.dataset.runState === 'done')",
  "queued sketches to run in order"
);
assert.deepEqual(
  await evaluate("[...document.querySelectorAll('[data-snippet-output]')[1].children].map((node) => node.textContent)"),
  ["Hello", "Hello"]
);
assert.deepEqual(
  await evaluate("[...document.querySelectorAll('[data-snippet-output]')[2].children].map((node) => node.textContent)"),
  ["neon", "violet"]
);

await evaluate(sourceEditExpression(3, `import lib/std.dl

print "Found it." as line`));
await evaluate(`(() => {
  const source = document.querySelectorAll('[data-runnable-sketch]')[3].querySelector('[data-snippet-source]');
  source.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', ctrlKey: true, bubbles: true }));
})()`);
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]')[3].dataset.runState === 'done'",
  "the keyboard-triggered sketch run"
);
assert.equal(
  await evaluate("document.querySelectorAll('[data-snippet-output]')[3].querySelector('span').textContent"),
  "Found it."
);

await evaluate(sourceEditExpression(3, "this phrase does not exist"));
await evaluate("document.querySelectorAll('[data-snippet-run]')[3].click()");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]')[3].dataset.runState === 'error'",
  "invalid source to report a compile error"
);
const compileFailure = await evaluate(`(() => {
  const sketch = document.querySelectorAll('[data-runnable-sketch]')[3];
  const diagnostics = sketch.querySelector('[data-snippet-diagnostics]');
  return {
    diagnosticsHidden: diagnostics.hidden,
    diagnostics: diagnostics.textContent,
    output: sketch.querySelector('[data-snippet-output] span').textContent
  };
})()`);
assert.equal(compileFailure.diagnosticsHidden, false);
assert.ok(compileFailure.diagnostics.length > 0);
assert.equal(compileFailure.output, "Found it.", "A failed compile must never run a stale artifact");

await evaluate("document.querySelector('[data-lab-tab=\"reuse\"]').click()");
await evaluate(sourceEditExpression(4, `import lib/std.dl

print "Words become tools." as line`));
await evaluate("document.querySelectorAll('[data-snippet-run]')[4].click()");
await waitFor(
  "document.querySelectorAll('[data-runnable-sketch]')[4].dataset.runState === 'done'",
  "the active language sketch to run"
);
assert.equal(
  await evaluate("document.querySelectorAll('[data-snippet-output]')[4].textContent.trim()"),
  "Words become tools."
);
assert.equal(
  requestedUrls.filter((url) => url.endsWith("/compiler/compiler-worker.js")).length,
  1,
  "Homepage edits and runs must reuse one compiler worker"
);
for (const section of ["sketches", "language"]) {
  await evaluate(`document.querySelector('#${section}').scrollIntoView()`);
  await new Promise((resolve) => setTimeout(resolve, 1200));
  await captureScreenshot(`homepage-${section}`);
}

await navigate(volumetricShaderEditorPath);
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
    pointCount: Number(canvas.dataset.previewGeometryPoints),
    canvasHeight: canvas.getBoundingClientRect().height,
    shellHeight: shell.getBoundingClientRect().height,
    toolPanelHeight: toolPanel.getBoundingClientRect().height,
    scrollbarColor: slider ? getComputedStyle(slider).backgroundColor : '',
    tokenColorCount: tokenColors.size
  };
})()`);
assert.equal(initialShaderState.mode, "shader");
assert.ok(initialShaderState.revision >= 1);
assert.equal(initialShaderState.pointCount, shaderManifest.scenes[2].geometry.pointCount);
assert.ok(
  initialShaderState.canvasHeight >= initialShaderState.toolPanelHeight * 0.75
    && Math.abs(initialShaderState.canvasHeight - initialShaderState.shellHeight) <= 1,
  `The live shader must fill the available preview frame: ${JSON.stringify(initialShaderState)}`
);
assert.notEqual(initialShaderState.scrollbarColor, "rgb(255, 255, 255)", "The IDE scrollbar must not be white");
assert.ok(initialShaderState.tokenColorCount >= 3, "The shader source must be syntax highlighted");
await hoverMonacoText("scene");
await waitFor(
  "[...document.querySelectorAll('.monaco-hover:not(.hidden)')].some((hover) => hover.textContent.trim().length > 0)",
  "the shader IDE to display DynLex hover information",
  10000
);
await captureScreenshot("ide-shader-initial");

await replaceMonacoSource(`import lib/shader.dl

if this is a vertex shader:
    set the output position to the vertex x the vertex y the vertex z 1.0
else:
    set pulse to the shader time
    set pulse to the sine of pulse
    set pulse to pulse * 0.5 + 0.5
    set pass_glow to the shader render pass * 0.22
    set the fragment color to (pulse + pass_glow) 0.12 0.72 1.0
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
await findMonacoText("square 8");
await waitFor(
  "document.querySelector('.line-numbers.active-line-number')?.textContent.trim() === '7'",
  "the square invocation to be selected"
);
await dispatchKey("F12", "F12", 123);
await waitFor(
  "document.querySelector('.line-numbers.active-line-number')?.textContent.trim() === '3'",
  "F12 to navigate from the square invocation to its definition"
);
await findMonacoText("print square 8");
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
  "document.querySelector('.view-lines').textContent.replace(/\\u00a0/g, ' ').includes('print square 8 as line')",
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
await waitFor(
  "document.querySelector('[data-live-shader-banner]')?.dataset.shaderPlaylistReady === 'true'",
  "the ultrawide shader banner"
);
const ultrawideThoughtLayout = await evaluate(`(() => {
  const codeRect = document.querySelector('[data-shader-code]').getBoundingClientRect();
  const cloudGuide = document.querySelector('.thought-assembly');
  const cloudRect = cloudGuide.getBoundingClientRect();
  return {
    cloudWidthInScreens: cloudRect.width / codeRect.width,
    gapInScreens: (cloudRect.left - codeRect.right) / codeRect.width,
    guideIsTransparent: (
      getComputedStyle(cloudGuide).backgroundImage === 'none'
      && getComputedStyle(cloudGuide).backgroundColor === 'rgba(0, 0, 0, 0)'
      && getComputedStyle(cloudGuide).clipPath === 'none'
    )
  };
})()`);
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
await captureScreenshot("homepage-mobile");
await command("Emulation.clearDeviceMetricsOverride");

assert.deepEqual(
  runtimeExceptions,
  [],
  "Homepage and IDE interactions must not raise uncaught browser exceptions"
);

socket.close();
console.log("Homepage sketches and IDE compile and run in Chrome.");
