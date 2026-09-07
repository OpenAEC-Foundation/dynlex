import {
  createShaderPreview,
  validateShaderGeometryDescriptor
} from "./shader-renderer.js";
import { isGeneratedTerrainGeometryDescriptor } from "./terrain-geometry.js";
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
    manifest?.schemaVersion !== 13
    || !manifest.semanticLegend
    || !Array.isArray(manifest.scenes)
    || manifest.scenes.length < 3
  ) {
    throw new Error("Invalid homepage shader manifest");
  }
  const ids = new Set();
  for (const scene of manifest.scenes) {
    const validStage = (stage) => (
      typeof stage?.sources?.webgpu?.path === "string"
      && typeof stage?.sources?.webgl?.path === "string"
      && Array.isArray(stage.uniforms)
    );
    if (
      typeof scene.id !== "string"
      || typeof scene.title !== "string"
      || typeof scene.durationSeconds !== "number"
      || scene.durationSeconds <= 0
      || typeof scene.source !== "string"
      || !validStage(scene.shaders?.fragment)
      || !Array.isArray(scene.semanticTokens)
    ) {
      throw new Error("Invalid homepage shader record");
    }
    const hasVertexShader = scene.shaders.vertex !== undefined;
    const hasGeometry = scene.geometry !== undefined;
    if (
      hasVertexShader !== hasGeometry
      || (hasVertexShader && !validStage(scene.shaders.vertex))
    ) {
      throw new Error("Homepage shader geometry and vertex source must be configured together");
    }
    if (hasGeometry) {
      if (isGeneratedTerrainGeometryDescriptor(scene.geometry)) {
        if (scene.geometry.path !== undefined || scene.geometry.indices !== undefined) {
          throw new Error("Generated homepage geometry must not include fixed assets");
        }
      } else if (
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

async function loadText(relativePath, backend, stage) {
  const response = await fetch(new URL(relativePath, import.meta.url));
  if (!response.ok) {
    throw new Error(`Unable to load generated shader: ${relativePath}`);
  }
  const source = await response.text();
  if (
    (backend === "webgpu" && (!source.includes("fn main") || !source.includes(`@${stage}`)))
    || (backend === "webgl" && (!source.startsWith("#version 300 es") || !source.includes("void main")))
  ) {
    throw new Error(`Generated shader is invalid: ${relativePath}`);
  }
  return source;
}

async function loadStageSources(stage, stageName, backend) {
  const source = await loadText(stage.sources[backend].path, backend, stageName);
  return Object.freeze({ [backend]: source });
}

async function loadBinary(relativePath) {
  const response = await fetch(new URL(relativePath, import.meta.url));
  if (!response.ok) {
    throw new Error(`Unable to load shader geometry: ${relativePath}`);
  }
  return response.arrayBuffer();
}

async function loadSceneProgram(scene, backend) {
  const fragmentSources = await loadStageSources(scene.shaders.fragment, "fragment", backend);
  if (!scene.geometry) {
    return Object.freeze({
      fragmentSources,
      fragmentUniforms: scene.shaders.fragment.uniforms
    });
  }
  const vertexSources = await loadStageSources(scene.shaders.vertex, "vertex", backend);
  if (isGeneratedTerrainGeometryDescriptor(scene.geometry)) {
    return Object.freeze({
      fragmentSources,
      fragmentUniforms: scene.shaders.fragment.uniforms,
      vertexSources,
      vertexUniforms: scene.shaders.vertex.uniforms,
      geometry: scene.geometry
    });
  }
  const [data, indexData] = await Promise.all([
    loadBinary(scene.geometry.path),
    scene.geometry.indices ? loadBinary(scene.geometry.indices.path) : null
  ]);
  const indices = scene.geometry.indices
    ? Object.freeze({ ...scene.geometry.indices, data: indexData })
    : undefined;
  return Object.freeze({
    fragmentSources,
    fragmentUniforms: scene.shaders.fragment.uniforms,
    vertexSources,
    vertexUniforms: scene.shaders.vertex.uniforms,
    geometry: Object.freeze({ ...scene.geometry, data, ...(indices ? { indices } : {}) })
  });
}

function smooth(lower, upper, value) {
  const normalized = Math.max(0, Math.min(1, (value - lower) / (upper - lower)));
  return normalized * normalized * (3 - 2 * normalized);
}

const INCOMING_CODE_START = 0.7;
const INCOMING_THOUGHT_START = 0.79;
const INCOMING_EXPANSION_START = 0.84;
const INCOMING_EXPANSION_END = 0.99;

function measureBannerGeometry(section, code) {
  const sectionRect = section.getBoundingClientRect();
  const codeRect = code.getBoundingClientRect();
  const guideWidth = codeRect.width * 0.78;
  const guideHeight = guideWidth / 1.42;
  const naturalLeft = codeRect.right - sectionRect.left + codeRect.width * 0.04;
  const naturalTop = codeRect.top - sectionRect.top - guideHeight * 0.22;
  const guideLeft = Math.min(naturalLeft, sectionRect.width - guideWidth * 0.72);
  const guideTop = Math.max(72, naturalTop);
  const origin = {
    x: codeRect.left - sectionRect.left + codeRect.width * 0.8,
    y: codeRect.top - sectionRect.top + codeRect.height * 0.62
  };
  const target = {
    x: guideLeft + guideWidth * 0.08,
    y: guideTop + guideHeight * 0.74
  };
  const control = {
    x: origin.x + (target.x - origin.x) * 0.52,
    y: Math.min(origin.y, target.y) - sectionRect.height * 0.055
  };
  return {
    frameWidth: sectionRect.width,
    frameHeight: sectionRect.height,
    guideLeft,
    guideTop,
    guideWidth,
    guideHeight,
    origin,
    cloudPoint: pointOnQuadraticCurve(origin, control, target, 0.84),
    middlePoint: pointOnQuadraticCurve(origin, control, target, 0.46),
    scrollRange: Math.max(0, code.scrollHeight - code.clientHeight)
  };
}

function pointOnQuadraticCurve(origin, control, target, progress) {
  const inverse = 1 - progress;
  return {
    x: inverse * inverse * origin.x + 2 * inverse * progress * control.x + progress * progress * target.x,
    y: inverse * inverse * origin.y + 2 * inverse * progress * control.y + progress * progress * target.y
  };
}

function applyBannerGeometry(geometry, thoughtAssembly, thoughtTail) {
  thoughtAssembly.style.left = `${geometry.guideLeft}px`;
  thoughtAssembly.style.top = `${geometry.guideTop}px`;
  thoughtAssembly.style.width = `${geometry.guideWidth}px`;
  thoughtAssembly.style.height = `${geometry.guideHeight}px`;
  thoughtTail.style.setProperty("--tail-cloud-x", `${geometry.cloudPoint.x}px`);
  thoughtTail.style.setProperty("--tail-cloud-y", `${geometry.cloudPoint.y}px`);
  thoughtTail.style.setProperty("--tail-middle-x", `${geometry.middlePoint.x}px`);
  thoughtTail.style.setProperty("--tail-middle-y", `${geometry.middlePoint.y}px`);
  thoughtTail.style.setProperty("--tail-origin-x", `${geometry.origin.x}px`);
  thoughtTail.style.setProperty("--tail-origin-y", `${geometry.origin.y}px`);
}

function transitionBounds(geometry, expansion) {
  const inverse = 1 - expansion;
  return {
    left: geometry.guideLeft * inverse - geometry.frameWidth * expansion,
    top: geometry.guideTop * inverse - geometry.frameHeight * expansion,
    width: geometry.guideWidth * inverse + geometry.frameWidth * 3 * expansion,
    height: geometry.guideHeight * inverse + geometry.frameHeight * 3 * expansion,
    frameWidth: geometry.frameWidth,
    frameHeight: geometry.frameHeight
  };
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

  const manifestResponse = await fetch(new URL("./shaders/manifest.json", import.meta.url));
  if (!manifestResponse.ok) {
    throw new Error("Unable to load the homepage shader manifest");
  }
  const manifest = validateManifest(await manifestResponse.json());
  const layer = required("[data-shader-layer]", section);
  const canvas = required('[data-shader-canvas="immersive"]', layer);
  let bannerGeometry = measureBannerGeometry(section, shaderCode);
  applyBannerGeometry(bannerGeometry, thoughtAssembly, thoughtTail);
  let activeStartedAt = performance.now();
  let preparedStartedAt = activeStartedAt;
  const preview = await createShaderPreview(canvas, {
    running: false,
    geometryHorizontalPixels() {
      return Math.max(
        1,
        Math.ceil(bannerGeometry.frameWidth * (window.devicePixelRatio || 1))
      );
    },
    elapsedSeconds(timestamp, role) {
      const startedAt = role === "prepared" ? preparedStartedAt : activeStartedAt;
      return Math.max(0, (timestamp - startedAt) / 1000);
    }
  });
  const scenePrograms = await Promise.all(
    manifest.scenes.map((scene) => loadSceneProgram(scene, preview.backend))
  );

  let activeIndex = 0;
  let incomingSceneIndex = null;
  let sceneStartedAt = performance.now();
  let bannerVisible = bannerIntersectsViewport();
  let timelineProgress = 0;
  let timelineFrameRequest = 0;
  let timelineResumeFrameRequest = 0;
  let timelineResumeStartedAt = null;
  let timelineResumeGeneration = 0;
  let timelineWaitingForPreload = true;
  let pausedAt = bannerVisible ? null : performance.now();
  let preloadGeneration = 0;

  function bannerIntersectsViewport() {
    const rect = section.getBoundingClientRect();
    return (
      rect.bottom > 0
      && rect.top < window.innerHeight
      && rect.right > 0
      && rect.left < window.innerWidth
    );
  }

  function syncPreviewActivity() {
    preview.setRunning(bannerVisible);
  }

  function stopTimelineAnimation(timestamp = performance.now()) {
    timelineResumeGeneration += 1;
    if (timelineResumeStartedAt !== null) {
      sceneStartedAt += timestamp - timelineResumeStartedAt;
      timelineResumeStartedAt = null;
    }
    cancelAnimationFrame(timelineFrameRequest);
    cancelAnimationFrame(timelineResumeFrameRequest);
    timelineFrameRequest = 0;
    timelineResumeFrameRequest = 0;
  }

  function scheduleTimelineAnimation() {
    if (
      timelineFrameRequest
      || timelineResumeStartedAt !== null
      || timelineWaitingForPreload
      || !bannerVisible
      || reducedMotion.matches
    ) {
      return;
    }
    timelineFrameRequest = requestAnimationFrame(animate);
  }

  function scheduleTimelineResume() {
    if (
      timelineFrameRequest
      || timelineResumeStartedAt !== null
      || timelineWaitingForPreload
      || !bannerVisible
      || reducedMotion.matches
    ) {
      return;
    }
    timelineResumeStartedAt = performance.now();
    const generation = ++timelineResumeGeneration;
    preview.whenNextFrameRendered().then(() => {
      if (
        generation !== timelineResumeGeneration
        || !bannerVisible
        || reducedMotion.matches
      ) {
        return;
      }
      timelineResumeFrameRequest = requestAnimationFrame((timestamp) => {
        if (generation !== timelineResumeGeneration) return;
        timelineResumeFrameRequest = 0;
        sceneStartedAt += timestamp - timelineResumeStartedAt;
        timelineResumeStartedAt = null;
        scheduleTimelineAnimation();
      });
    }).catch((error) => {
      console.error("Homepage shader resume failed", error);
      section.dataset.shaderPlaylistReady = "false";
    });
  }

  function updateBannerVisibility(nextVisible) {
    if (nextVisible === bannerVisible) return;
    const timestamp = performance.now();
    if (!nextVisible) {
      bannerVisible = false;
      pausedAt = timestamp;
      stopTimelineAnimation(timestamp);
      syncPreviewActivity();
      return;
    }
    if (pausedAt === null) {
      throw new Error("Visible shader banner has no paused timeline");
    }
    const pausedMilliseconds = timestamp - pausedAt;
    sceneStartedAt += pausedMilliseconds;
    activeStartedAt += pausedMilliseconds;
    if (incomingSceneIndex !== null) preparedStartedAt += pausedMilliseconds;
    pausedAt = null;
    bannerVisible = true;
    setTimeline(timelineProgress);
    scheduleTimelineResume();
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
    bannerGeometry = measureBannerGeometry(section, shaderCode);
    applyBannerGeometry(bannerGeometry, thoughtAssembly, thoughtTail);
    const params = new URLSearchParams({
      mode: "shader",
      scene: scene.id
    });
    editorLink.href = `ide/index.html?${params}`;
  }

  async function installActiveScene(sceneIndex, startedAt) {
    const installed = await preview.replaceProgram(scenePrograms[sceneIndex]);
    if (!installed) return false;
    activeStartedAt = startedAt;
    return true;
  }

  async function installPreparedScene(sceneIndex) {
    return preview.prepareProgram(scenePrograms[sceneIndex]);
  }

  async function preloadNextScene() {
    if (incomingSceneIndex !== null) {
      throw new Error("Cannot preload a shader while another shader is being revealed");
    }
    const nextSceneIndex = (activeIndex + 1) % manifest.scenes.length;
    const installed = await installPreparedScene(nextSceneIndex);
    if (!installed) return false;
    section.dataset.preloadedShaderIndex = String(nextSceneIndex);
    nextButton.disabled = false;
    return true;
  }

  function scheduleNextScenePreload() {
    const generation = ++preloadGeneration;
    timelineWaitingForPreload = true;
    delete section.dataset.preloadedShaderIndex;
    nextButton.disabled = true;
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        if (generation !== preloadGeneration) return;
        void preloadNextScene().then((installed) => {
          if (!installed || generation !== preloadGeneration) return;
          timelineWaitingForPreload = false;
          sceneStartedAt = performance.now();
          timelineProgress = 0;
          if (!bannerVisible) pausedAt = sceneStartedAt;
          scheduleTimelineAnimation();
        }).catch((error) => {
          console.error("Homepage shader preload failed", error);
          section.dataset.shaderPlaylistReady = "false";
        });
      });
    });
  }

  function prepareIncomingScene() {
    if (incomingSceneIndex !== null) return;
    const nextSceneIndex = (activeIndex + 1) % manifest.scenes.length;
    if (section.dataset.preloadedShaderIndex !== String(nextSceneIndex)) {
      throw new Error("The incoming shader was not preloaded");
    }
    incomingSceneIndex = nextSceneIndex;
    preloadGeneration += 1;
    delete section.dataset.preloadedShaderIndex;
    preparedStartedAt = performance.now();
    preview.beginTransition();
    preview.setTransition(transitionBounds(bannerGeometry, 0));
    updateLaptopReadout(manifest.scenes[nextSceneIndex]);
    section.dataset.incomingShaderIndex = String(nextSceneIndex);
    section.dataset.incomingShader = manifest.scenes[nextSceneIndex].id;
    nextButton.disabled = true;
    syncPreviewActivity();
  }

  function discardIncomingScene() {
    if (incomingSceneIndex === null) return;
    preview.discardPrepared();
    incomingSceneIndex = null;
    delete section.dataset.incomingShaderIndex;
    delete section.dataset.incomingShader;
    scheduleNextScenePreload();
  }

  function setTimeline(progress) {
    timelineProgress = progress;
    section.style.setProperty("--shader-progress", progress.toFixed(4));
    section.dataset.sceneProgress = progress.toFixed(4);
    let immersionOpacity = 0;
    let laptopOpacity = 0;
    let laptopCodeOpacity = 0;
    let thoughtTailOpacity = 0;
    let cloudCoverage = "viewport";
    let scenePhase = "immersive";

    if (incomingSceneIndex !== null) {
      const codeArrival = smooth(INCOMING_CODE_START, 0.78, progress);
      const thoughtArrival = smooth(INCOMING_THOUGHT_START, 0.86, progress);
      const expansion = smooth(INCOMING_EXPANSION_START, INCOMING_EXPANSION_END, progress);
      const detailDeparture = smooth(0.91, 0.985, progress);
      preview.setTransition(transitionBounds(bannerGeometry, expansion));
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

    shaderCode.scrollTop = bannerGeometry.scrollRange * (
      incomingSceneIndex === null
        ? smooth(0.08, 0.48, progress)
        : smooth(INCOMING_CODE_START, 0.96, progress)
    );
    syncPreviewActivity();
  }

  function promoteIncomingScene(timestamp) {
    if (incomingSceneIndex === null) {
      throw new Error("Shader transition completed without an incoming layer");
    }
    preview.promotePrepared();
    activeIndex = incomingSceneIndex;
    incomingSceneIndex = null;
    activeStartedAt = preparedStartedAt;
    sceneStartedAt = timestamp;
    updateActiveReadout(manifest.scenes[activeIndex], activeIndex);
    delete section.dataset.incomingShaderIndex;
    delete section.dataset.incomingShader;
    nextButton.disabled = true;
    setTimeline(0);
    scheduleNextScenePreload();
  }

  function showReducedScene(index) {
    if (
      index !== (activeIndex + 1) % manifest.scenes.length
      || section.dataset.preloadedShaderIndex !== String(index)
    ) {
      throw new Error("Reduced-motion navigation requires a preloaded shader");
    }
    preloadGeneration += 1;
    delete section.dataset.preloadedShaderIndex;
    incomingSceneIndex = index;
    preparedStartedAt = performance.now();
    updateLaptopReadout(manifest.scenes[index]);
    promoteIncomingScene(preparedStartedAt);
  }

  function advanceScene() {
    if (reducedMotion.matches) {
      showReducedScene((activeIndex + 1) % manifest.scenes.length);
      return;
    }
    if (incomingSceneIndex !== null) return;
    const timestamp = performance.now();
    const durationMilliseconds = manifest.scenes[activeIndex].durationSeconds * 1000;
    sceneStartedAt = timestamp - durationMilliseconds * INCOMING_CODE_START;
    prepareIncomingScene();
    setTimeline(INCOMING_CODE_START);
  }

  function animate(timestamp) {
    timelineFrameRequest = 0;
    if (!bannerVisible || reducedMotion.matches) return;
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
    scheduleTimelineAnimation();
  }

  const visibilityObserver = new IntersectionObserver(() => {
    updateBannerVisibility(bannerIntersectsViewport());
  }, { threshold: 0.01 });
  visibilityObserver.observe(section);
  const geometryObserver = new ResizeObserver(() => {
    bannerGeometry = measureBannerGeometry(section, shaderCode);
    applyBannerGeometry(bannerGeometry, thoughtAssembly, thoughtTail);
    if (incomingSceneIndex !== null) setTimeline(timelineProgress);
  });
  geometryObserver.observe(section);
  geometryObserver.observe(shaderCode);

  nextButton.addEventListener("click", advanceScene);
  window.addEventListener("scroll", () => {
    updateBannerVisibility(bannerIntersectsViewport());
  }, { passive: true });
  window.addEventListener("resize", () => {
    updateBannerVisibility(bannerIntersectsViewport());
  });
  reducedMotion.addEventListener("change", () => {
    stopTimelineAnimation();
    discardIncomingScene();
    sceneStartedAt = performance.now();
    activeStartedAt = sceneStartedAt;
    if (!bannerVisible) pausedAt = sceneStartedAt;
    updateLaptopReadout(manifest.scenes[activeIndex]);
    setTimeline(0);
    scheduleTimelineAnimation();
  });

  await installActiveScene(activeIndex, performance.now());
  sceneStartedAt = performance.now();
  activeStartedAt = sceneStartedAt;
  updateActiveReadout(manifest.scenes[activeIndex], activeIndex);
  updateLaptopReadout(manifest.scenes[activeIndex]);
  setTimeline(0);
  section.dataset.shaderPlaylistReady = "true";
  scheduleNextScenePreload();
  scheduleTimelineAnimation();
}
