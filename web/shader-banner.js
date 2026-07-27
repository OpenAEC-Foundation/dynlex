import {
  createShaderPreview,
  validateShaderGeometryDescriptor
} from "./shader-renderer.js";
import { renderSemanticTokens } from "./semantic-highlighting.js";

function required(selector, scope) {
  const element = scope.querySelector(selector);
  if (!element) {
    throw new Error(`Missing required live-shader element: ${selector}`);
  }
  return element;
}

function validateManifest(manifest) {
  if (
    manifest?.schemaVersion !== 8
    || !manifest.semanticLegend
    || !Array.isArray(manifest.scenes)
    || manifest.scenes.length < 3
  ) {
    throw new Error("Invalid homepage shader manifest");
  }
  const ids = new Set();
  for (const scene of manifest.scenes) {
    if (
      typeof scene.id !== "string"
      || typeof scene.title !== "string"
      || typeof scene.durationSeconds !== "number"
      || scene.durationSeconds <= 0
      || typeof scene.source !== "string"
      || typeof scene.shaders?.fragment?.path !== "string"
      || !Array.isArray(scene.uniforms)
      || !Array.isArray(scene.semanticTokens)
    ) {
      throw new Error("Invalid homepage shader record");
    }
    const hasVertexShader = typeof scene.shaders.vertex?.path === "string";
    const hasGeometry = scene.geometry !== undefined;
    if (hasVertexShader !== hasGeometry) {
      throw new Error("Homepage shader geometry and vertex source must be configured together");
    }
    if (hasGeometry) {
      if (
        typeof scene.geometry.path !== "string"
        || (
          scene.geometry.indices !== undefined
          && typeof scene.geometry.indices.path !== "string"
        )
      ) {
        throw new Error("Invalid homepage shader geometry");
      }
      validateShaderGeometryDescriptor(scene.geometry);
    }
    if (ids.has(scene.id)) {
      throw new Error("Duplicate homepage shader id");
    }
    ids.add(scene.id);
  }
  return manifest;
}

async function loadText(relativePath) {
  const response = await fetch(relativePath);
  if (!response.ok) {
    throw new Error(`Unable to load generated shader: ${relativePath}`);
  }
  const source = await response.text();
  if (!source.startsWith("#version 300 es") || !source.includes("void main")) {
    throw new Error(`Generated shader is invalid: ${relativePath}`);
  }
  return source;
}

async function loadBinary(relativePath) {
  const response = await fetch(relativePath);
  if (!response.ok) {
    throw new Error(`Unable to load shader geometry: ${relativePath}`);
  }
  return response.arrayBuffer();
}

async function loadSceneProgram(scene) {
  const fragmentSource = await loadText(scene.shaders.fragment.path);
  if (!scene.geometry) {
    return Object.freeze({ fragmentSource });
  }
  const [vertexSource, data, indexData] = await Promise.all([
    loadText(scene.shaders.vertex.path),
    loadBinary(scene.geometry.path),
    scene.geometry.indices ? loadBinary(scene.geometry.indices.path) : null
  ]);
  const indices = scene.geometry.indices
    ? Object.freeze({ ...scene.geometry.indices, data: indexData })
    : undefined;
  return Object.freeze({
    fragmentSource,
    vertexSource,
    geometry: Object.freeze({ ...scene.geometry, data, ...(indices ? { indices } : {}) })
  });
}

function smooth(lower, upper, value) {
  const normalized = Math.max(0, Math.min(1, (value - lower) / (upper - lower)));
  return normalized * normalized * (3 - 2 * normalized);
}

const CLOUD_COVER_TRANSFORM = "translate(-1 -1) scale(3 3)";
const INCOMING_CODE_START = 0.7;
const INCOMING_THOUGHT_START = 0.79;
const INCOMING_EXPANSION_START = 0.84;
const INCOMING_EXPANSION_END = 0.99;

function formatTransformNumber(value) {
  return Number(value.toFixed(5)).toString();
}

function visibleCloudGeometry(section, thoughtAssembly, expansion) {
  const sectionRect = section.getBoundingClientRect();
  const guideRect = thoughtAssembly.getBoundingClientRect();
  const desiredLeft = (guideRect.left - sectionRect.left) * (1 - expansion)
    - sectionRect.width * expansion;
  const desiredTop = (guideRect.top - sectionRect.top) * (1 - expansion)
    - sectionRect.height * expansion;
  const desiredWidth = guideRect.width * (1 - expansion) + sectionRect.width * 3 * expansion;
  const desiredHeight = guideRect.height * (1 - expansion) + sectionRect.height * 3 * expansion;
  const left = Math.max(0, desiredLeft);
  const top = Math.max(0, desiredTop);
  const right = Math.min(sectionRect.width, desiredLeft + desiredWidth);
  const bottom = Math.min(sectionRect.height, desiredTop + desiredHeight);
  const width = right - left;
  const height = bottom - top;
  const translateX = (desiredLeft - left) / width;
  const translateY = (desiredTop - top) / height;
  const scaleX = desiredWidth / width;
  const scaleY = desiredHeight / height;
  return {
    left,
    top,
    width,
    height,
    transform: expansion === 1
      ? CLOUD_COVER_TRANSFORM
      : `translate(${formatTransformNumber(translateX)} ${formatTransformNumber(translateY)}) scale(${formatTransformNumber(scaleX)} ${formatTransformNumber(scaleY)})`
  };
}

function setRevealGeometry(section, thoughtAssembly, layer, expansion) {
  const geometry = visibleCloudGeometry(section, thoughtAssembly, expansion);
  layer.element.style.left = `${geometry.left}px`;
  layer.element.style.top = `${geometry.top}px`;
  layer.element.style.width = `${geometry.width}px`;
  layer.element.style.height = `${geometry.height}px`;
  layer.path.setAttribute("transform", geometry.transform);
}

function setFullGeometry(section, layer) {
  layer.element.style.left = "0px";
  layer.element.style.top = "0px";
  layer.element.style.width = `${section.clientWidth}px`;
  layer.element.style.height = `${section.clientHeight}px`;
  layer.path.setAttribute("transform", CLOUD_COVER_TRANSFORM);
}

function pointOnQuadraticCurve(origin, control, target, progress) {
  const inverse = 1 - progress;
  return {
    x: inverse * inverse * origin.x + 2 * inverse * progress * control.x + progress * progress * target.x,
    y: inverse * inverse * origin.y + 2 * inverse * progress * control.y + progress * progress * target.y
  };
}

function updateThoughtAssembly(section, code, thoughtAssembly) {
  const sectionRect = section.getBoundingClientRect();
  const codeRect = code.getBoundingClientRect();
  const width = codeRect.width * 0.78;
  const height = width / 1.42;
  const naturalLeft = codeRect.right - sectionRect.left + codeRect.width * 0.04;
  const naturalTop = codeRect.top - sectionRect.top - height * 0.22;
  const left = Math.min(naturalLeft, sectionRect.width - width * 0.72);
  const top = Math.max(72, naturalTop);
  thoughtAssembly.style.left = `${left}px`;
  thoughtAssembly.style.top = `${top}px`;
  thoughtAssembly.style.width = `${width}px`;
  thoughtAssembly.style.height = `${height}px`;
}

function updateThoughtTail(section, code, thoughtAssembly, thoughtTail) {
  const sectionRect = section.getBoundingClientRect();
  const codeRect = code.getBoundingClientRect();
  const guideRect = thoughtAssembly.getBoundingClientRect();
  const origin = {
    x: codeRect.left - sectionRect.left + codeRect.width * 0.8,
    y: codeRect.top - sectionRect.top + codeRect.height * 0.62
  };
  const target = {
    x: guideRect.left - sectionRect.left + guideRect.width * 0.08,
    y: guideRect.top - sectionRect.top + guideRect.height * 0.74
  };
  const control = {
    x: origin.x + (target.x - origin.x) * 0.52,
    y: Math.min(origin.y, target.y) - sectionRect.height * 0.055
  };
  const cloudPoint = pointOnQuadraticCurve(origin, control, target, 0.84);
  const middlePoint = pointOnQuadraticCurve(origin, control, target, 0.46);
  thoughtTail.style.setProperty("--tail-cloud-x", `${cloudPoint.x}px`);
  thoughtTail.style.setProperty("--tail-cloud-y", `${cloudPoint.y}px`);
  thoughtTail.style.setProperty("--tail-middle-x", `${middlePoint.x}px`);
  thoughtTail.style.setProperty("--tail-middle-y", `${middlePoint.y}px`);
  thoughtTail.style.setProperty("--tail-origin-x", `${origin.x}px`);
  thoughtTail.style.setProperty("--tail-origin-y", `${origin.y}px`);
}

export async function createShaderBanner(section) {
  const thoughtAssembly = required(".thought-assembly", section);
  const thoughtTail = required(".thought-tail", section);
  const shaderName = required("[data-shader-name]", section);
  const shaderIndex = required("[data-shader-index]", section);
  const shaderFile = required("[data-shader-file]", section);
  const shaderCode = required("[data-shader-code]", section);
  const editorLink = required("[data-shader-editor-link]", section);
  const nextButton = required("[data-shader-next]", section);
  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  nextButton.disabled = true;

  const manifestResponse = await fetch("shaders/manifest.json");
  if (!manifestResponse.ok) {
    throw new Error("Unable to load the homepage shader manifest");
  }
  const manifest = validateManifest(await manifestResponse.json());
  const scenePrograms = await Promise.all(manifest.scenes.map(loadSceneProgram));

  const layerElements = [...section.querySelectorAll("[data-shader-layer]")];
  const pathElements = [...section.querySelectorAll("[data-thought-cloud-path]")];
  if (layerElements.length !== 2 || pathElements.length !== 2) {
    throw new Error("The live shader banner requires exactly two render layers");
  }

  const layers = layerElements.map((element, index) => {
    if (element.dataset.shaderLayer !== String(index)) {
      throw new Error("Live shader layers must use consecutive indices");
    }
    const path = required(`[data-thought-cloud-path="${index}"]`, section);
    const canvas = required('[data-shader-canvas="immersive"]', element);
    const layer = {
      element,
      path,
      canvas,
      preview: null,
      sceneIndex: null,
      startedAt: 0
    };
    layer.preview = createShaderPreview(canvas, {
      running: false,
      elapsedSeconds(timestamp) {
        return Math.max(0, (timestamp - layer.startedAt) / 1000);
      }
    });
    return layer;
  });

  let activeIndex = 0;
  let activeLayerIndex = 0;
  let incomingLayerIndex = null;
  let sceneStartedAt = performance.now();
  let bannerVisible = true;
  let timelineProgress = 0;
  let preloadGeneration = 0;

  function setLayerState(layer, state) {
    layer.element.dataset.layerState = state;
  }

  function syncPreviewActivity() {
    for (const layer of layers) {
      layer.preview.setRunning(bannerVisible && layer.element.dataset.layerState !== "dormant");
    }
  }

  function updateActiveReadout(scene, index) {
    shaderName.textContent = scene.title.toUpperCase();
    shaderIndex.textContent = `${String(index + 1).padStart(2, "0")} / ${String(manifest.scenes.length).padStart(2, "0")}`;
    section.dataset.activeShader = scene.id;
    section.dataset.activeShaderIndex = String(index);
  }

  function updateLaptopReadout(scene) {
    shaderFile.textContent = `${scene.id}.dl`;
    renderSemanticTokens(
      shaderCode,
      scene.source,
      scene.semanticTokens,
      manifest.semanticLegend,
      { baseClass: "shader-code-token", classPrefix: "shader-code-token-" }
    );
    const params = new URLSearchParams({
      mode: "shader",
      scene: scene.id
    });
    editorLink.href = `ide/index.html?${params}`;
  }

  function installScene(layer, sceneIndex, startedAt) {
    const scene = manifest.scenes[sceneIndex];
    layer.sceneIndex = sceneIndex;
    layer.startedAt = startedAt;
    layer.preview.replaceProgram(scenePrograms[sceneIndex], scene.uniforms);
  }

  function preloadNextScene() {
    if (incomingLayerIndex !== null) {
      throw new Error("Cannot preload a shader while another shader is being revealed");
    }
    const nextSceneIndex = (activeIndex + 1) % manifest.scenes.length;
    const preloadLayer = layers[1 - activeLayerIndex];
    if (preloadLayer.element.dataset.layerState !== "dormant") {
      throw new Error("The next shader must preload on the dormant render layer");
    }
    if (preloadLayer.sceneIndex !== nextSceneIndex) {
      installScene(preloadLayer, nextSceneIndex, performance.now());
    }
    section.dataset.preloadedShaderIndex = String(nextSceneIndex);
    nextButton.disabled = false;
  }

  function scheduleNextScenePreload() {
    const generation = ++preloadGeneration;
    delete section.dataset.preloadedShaderIndex;
    nextButton.disabled = true;
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        if (generation !== preloadGeneration) return;
        preloadNextScene();
      });
    });
  }

  function prepareIncomingScene() {
    if (incomingLayerIndex !== null) return;
    const nextSceneIndex = (activeIndex + 1) % manifest.scenes.length;
    incomingLayerIndex = 1 - activeLayerIndex;
    const incomingLayer = layers[incomingLayerIndex];
    if (
      incomingLayer.sceneIndex !== nextSceneIndex
      || section.dataset.preloadedShaderIndex !== String(nextSceneIndex)
    ) {
      throw new Error("The incoming shader was not preloaded");
    }
    preloadGeneration += 1;
    delete section.dataset.preloadedShaderIndex;
    setLayerState(incomingLayer, "revealing");
    updateThoughtAssembly(section, shaderCode, thoughtAssembly);
    setRevealGeometry(section, thoughtAssembly, incomingLayer, 0);
    incomingLayer.startedAt = performance.now();
    updateLaptopReadout(manifest.scenes[nextSceneIndex]);
    section.dataset.incomingShaderIndex = String(nextSceneIndex);
    section.dataset.incomingShader = manifest.scenes[nextSceneIndex].id;
    nextButton.disabled = true;
    syncPreviewActivity();
  }

  function discardIncomingScene() {
    if (incomingLayerIndex === null) return;
    const incomingLayer = layers[incomingLayerIndex];
    setLayerState(incomingLayer, "dormant");
    incomingLayer.preview.setRunning(false);
    incomingLayerIndex = null;
    delete section.dataset.incomingShaderIndex;
    delete section.dataset.incomingShader;
    preloadNextScene();
  }

  function setTimeline(progress) {
    timelineProgress = progress;
    section.style.setProperty("--shader-progress", progress.toFixed(4));
    section.dataset.sceneProgress = progress.toFixed(4);
    updateThoughtAssembly(section, shaderCode, thoughtAssembly);
    updateThoughtTail(section, shaderCode, thoughtAssembly, thoughtTail);

    const activeLayer = layers[activeLayerIndex];
    let immersionOpacity = 0;
    let laptopOpacity = 0;
    let laptopCodeOpacity = 0;
    let thoughtTailOpacity = 0;
    let cloudCoverage = "viewport";
    let scenePhase = "immersive";

    setLayerState(activeLayer, "active");
    setFullGeometry(section, activeLayer);

    if (incomingLayerIndex !== null) {
      const incomingLayer = layers[incomingLayerIndex];
      const codeArrival = smooth(INCOMING_CODE_START, 0.78, progress);
      const thoughtArrival = smooth(INCOMING_THOUGHT_START, 0.86, progress);
      const expansion = smooth(INCOMING_EXPANSION_START, INCOMING_EXPANSION_END, progress);
      const detailDeparture = smooth(0.91, 0.985, progress);
      setLayerState(incomingLayer, "revealing");
      setRevealGeometry(section, thoughtAssembly, incomingLayer, expansion);
      immersionOpacity = thoughtArrival;
      laptopOpacity = codeArrival * (1 - smooth(0.93, 0.995, progress));
      laptopCodeOpacity = smooth(INCOMING_CODE_START, 0.76, progress)
        * (1 - smooth(0.94, 0.995, progress));
      thoughtTailOpacity = thoughtArrival * (1 - detailDeparture);
      cloudCoverage = expansion === 1 ? "viewport" : "partial";
      scenePhase = progress < INCOMING_THOUGHT_START ? "next-code" : "next-thought";
    }

    section.style.setProperty("--laptop-opacity", laptopOpacity.toFixed(4));
    section.style.setProperty("--laptop-code-opacity", laptopCodeOpacity.toFixed(4));
    section.style.setProperty("--thought-tail-opacity", thoughtTailOpacity.toFixed(4));
    section.style.setProperty("--immersion-opacity", immersionOpacity.toFixed(4));
    section.style.setProperty("--shader-copy-opacity", (1 - smooth(0.47, 0.66, progress) * 0.16).toFixed(4));
    section.dataset.cloudCoverage = cloudCoverage;
    section.dataset.scenePhase = scenePhase;

    const scrollRange = Math.max(0, shaderCode.scrollHeight - shaderCode.clientHeight);
    shaderCode.scrollTop = scrollRange * (
      incomingLayerIndex === null
        ? smooth(0.08, 0.48, progress)
        : smooth(INCOMING_CODE_START, 0.96, progress)
    );
    syncPreviewActivity();
  }

  function promoteIncomingScene(timestamp) {
    if (incomingLayerIndex === null) {
      throw new Error("Shader transition completed without an incoming layer");
    }
    const previousLayer = layers[activeLayerIndex];
    const incomingLayer = layers[incomingLayerIndex];
    setLayerState(previousLayer, "dormant");
    previousLayer.preview.setRunning(false);
    setLayerState(incomingLayer, "active");
    setFullGeometry(section, incomingLayer);
    activeLayerIndex = incomingLayerIndex;
    activeIndex = incomingLayer.sceneIndex;
    incomingLayerIndex = null;
    sceneStartedAt = timestamp;
    updateActiveReadout(manifest.scenes[activeIndex], activeIndex);
    delete section.dataset.incomingShaderIndex;
    delete section.dataset.incomingShader;
    nextButton.disabled = true;
    setTimeline(0);
    scheduleNextScenePreload();
  }

  function showReducedScene(index) {
    const nextLayerIndex = 1 - activeLayerIndex;
    const nextLayer = layers[nextLayerIndex];
    if (
      index !== (activeIndex + 1) % manifest.scenes.length
      || nextLayer.sceneIndex !== index
      || section.dataset.preloadedShaderIndex !== String(index)
    ) {
      throw new Error("Reduced-motion navigation requires a preloaded shader");
    }
    preloadGeneration += 1;
    delete section.dataset.preloadedShaderIndex;
    incomingLayerIndex = nextLayerIndex;
    nextLayer.startedAt = performance.now();
    updateLaptopReadout(manifest.scenes[index]);
    promoteIncomingScene(nextLayer.startedAt);
  }

  function advanceScene() {
    if (reducedMotion.matches) {
      showReducedScene((activeIndex + 1) % manifest.scenes.length);
      return;
    }
    if (incomingLayerIndex !== null) return;
    const timestamp = performance.now();
    const durationMilliseconds = manifest.scenes[activeIndex].durationSeconds * 1000;
    sceneStartedAt = timestamp - durationMilliseconds * INCOMING_CODE_START;
    prepareIncomingScene();
    setTimeline(INCOMING_CODE_START);
  }

  function animate(timestamp) {
    if (reducedMotion.matches) {
      requestAnimationFrame(animate);
      return;
    }
    const scene = manifest.scenes[activeIndex];
    const elapsed = Math.max(0, (timestamp - sceneStartedAt) / 1000);
    const progress = Math.min(1, elapsed / scene.durationSeconds);
    if (progress >= INCOMING_CODE_START) {
      prepareIncomingScene();
    }
    if (progress === 1) {
      setTimeline(1);
      promoteIncomingScene(timestamp);
    } else {
      setTimeline(progress);
    }
    requestAnimationFrame(animate);
  }

  const visibilityObserver = new IntersectionObserver(([entry]) => {
    bannerVisible = entry.isIntersecting;
    syncPreviewActivity();
  }, { threshold: 0.01 });
  visibilityObserver.observe(section);

  nextButton.addEventListener("click", advanceScene);
  window.addEventListener("resize", () => setTimeline(timelineProgress));
  reducedMotion.addEventListener("change", () => {
    discardIncomingScene();
    sceneStartedAt = performance.now();
    layers[activeLayerIndex].startedAt = sceneStartedAt;
    updateLaptopReadout(manifest.scenes[activeIndex]);
    setTimeline(0);
  });

  const initialLayer = layers[activeLayerIndex];
  setLayerState(initialLayer, "active");
  setLayerState(layers[1 - activeLayerIndex], "dormant");
  setFullGeometry(section, initialLayer);
  installScene(initialLayer, activeIndex, performance.now());
  sceneStartedAt = performance.now();
  initialLayer.startedAt = sceneStartedAt;
  updateActiveReadout(manifest.scenes[activeIndex], activeIndex);
  updateLaptopReadout(manifest.scenes[activeIndex]);
  setTimeline(0);
  section.dataset.shaderPlaylistReady = "true";
  requestAnimationFrame(animate);
  scheduleNextScenePreload();
}
