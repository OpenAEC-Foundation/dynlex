import { semanticHighlightCache, semanticTokenLegend } from "./snippet-highlights.js";
import { semanticHighlightKey } from "./snippet-highlight-key.js";
import { renderSemanticTokens, semanticLegendsMatch } from "./semantic-highlighting.js";
import { createShaderBanner } from "./shader-banner.js";
import { initializeSiteNavigation } from "./site-navigation.js";
import {
  initializeLsp,
  LspClient,
  LspTextDocument,
  shutdownLsp
} from "./lsp-client.js";

function required(selector, scope = document) {
  const element = scope.querySelector(selector);
  if (!element) {
    throw new Error("Missing required homepage element: " + selector);
  }
  return element;
}

initializeSiteNavigation();
const compileMetric = required("[data-compile-metric]");

const tabList = required(".lab-tabs");
const tabs = [...tabList.querySelectorAll("[data-lab-tab]")];
const panels = [...document.querySelectorAll("[data-lab-panel]")];

function selectTab(selectedTab) {
  const selectedName = selectedTab.dataset.labTab;
  for (const tab of tabs) {
    const selected = tab === selectedTab;
    tab.setAttribute("aria-selected", String(selected));
    tab.tabIndex = selected ? 0 : -1;
  }
  for (const panel of panels) {
    panel.hidden = panel.dataset.labPanel !== selectedName;
  }
  const selectedSource = required(`[data-lab-panel="${selectedName}"] [data-snippet-source]`);
  const sketch = selectedTab.closest("[data-runnable-sketch]");
  if (sketch) {
    setSketchState(sketch, selectedSource.dataset.edited === "true" ? "edited" : "ready");
  }
}

tabList.addEventListener("click", (event) => {
  const tab = event.target.closest("[data-lab-tab]");
  if (tab) {
    selectTab(tab);
  }
});

tabList.addEventListener("keydown", (event) => {
  if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) {
    return;
  }
  event.preventDefault();
  const currentIndex = tabs.indexOf(document.activeElement);
  let nextIndex = currentIndex;
  if (event.key === "ArrowLeft") nextIndex = (currentIndex - 1 + tabs.length) % tabs.length;
  if (event.key === "ArrowRight") nextIndex = (currentIndex + 1) % tabs.length;
  if (event.key === "Home") nextIndex = 0;
  if (event.key === "End") nextIndex = tabs.length - 1;
  tabs[nextIndex].focus();
  selectTab(tabs[nextIndex]);
});

const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
await createShaderBanner(required("[data-live-shader-banner]"));

const reveals = [...document.querySelectorAll(".reveal")];
const revealObserver = new IntersectionObserver((entries, observer) => {
  for (const entry of entries) {
    if (!entry.isIntersecting) continue;
    entry.target.classList.add("is-visible");
    observer.unobserve(entry.target);
  }
}, { threshold: 0.14, rootMargin: "0px 0px -8%" });

for (const element of reveals) {
  revealObserver.observe(element);
}

const runnableSketches = [...document.querySelectorAll("[data-runnable-sketch]")];
let snippetWorker = null;
let snippetWorkerReady = null;
let snippetWorkerInitialized = false;
let snippetLsp = null;
let snippetLspDocument = null;
let nextSnippetRequestId = 1;
let nextSnippetVersion = 1;
let snippetCompilerQueue = Promise.resolve();
const pendingSnippetRequests = new Map();
const snippetHighlightStates = new WeakMap();
const runtimeSemanticHighlightCache = new Map();

function setSketchState(sketch, state) {
  const status = required("[data-snippet-status]", sketch);
  const labels = {
    ready: "READY",
    edited: "EDITED",
    queued: "QUEUED",
    loading: "LOADING",
    running: "RUNNING",
    done: "DONE",
    error: "ERROR"
  };
  if (!(state in labels)) {
    throw new Error(`Unknown snippet state: ${state}`);
  }
  sketch.dataset.runState = state;
  status.textContent = labels[state];
}

function createSnippetWorker() {
  snippetWorker = new Worker("/compiler/compiler-worker.js", { type: "module" });
  snippetWorker.addEventListener("message", (event) => {
    const message = event.data;
    if (!message || typeof message.id !== "number") {
      return;
    }
    const pending = pendingSnippetRequests.get(message.id);
    if (!pending) {
      return;
    }
    pendingSnippetRequests.delete(message.id);
    if (message.ok) {
      pending.resolve(message.payload);
    } else {
      pending.reject(new Error(message.error || "Worker request failed"));
    }
  });
  snippetWorker.addEventListener("error", (event) => {
    console.error("Homepage compiler worker failed", event.error || event.message);
  });
}

function callSnippetWorker(type, payload = {}) {
  if (!snippetWorker) {
    throw new Error("Homepage compiler worker has not been created");
  }
  const id = nextSnippetRequestId++;
  return new Promise((resolve, reject) => {
    pendingSnippetRequests.set(id, { resolve, reject });
    snippetWorker.postMessage({ id, type, payload });
  });
}

function ensureSnippetWorker() {
  if (!snippetWorkerReady) {
    createSnippetWorker();
    snippetWorkerReady = callSnippetWorker("init").then(async (result) => {
      snippetLsp = new LspClient((message) => callSnippetWorker("lsp.exchange", { message }));
      snippetLsp.onRequest("workspace/semanticTokens/refresh", () => null);
      const initializeResult = await initializeLsp(snippetLsp, {
        capabilities: {
          textDocument: {
            semanticTokens: {
              requests: { full: true }
            }
          },
          workspace: {
            semanticTokens: { refreshSupport: true }
          }
        }
      });
      const serverLegend = initializeResult.capabilities?.semanticTokensProvider?.legend;
      if (!semanticLegendsMatch(serverLegend, semanticTokenLegend)) {
        throw new Error("DynLex language server legend differs from the generated highlight cache");
      }
      snippetLspDocument = new LspTextDocument(snippetLsp, {
        uri: "file:///workspace/homepage-snippet.dl",
        languageId: "dynlex"
      });
      snippetWorkerInitialized = true;
      return result;
    });
  }
  return snippetWorkerReady;
}

async function syncSnippetLspDocument(sourceText) {
  if (!snippetLsp) {
    throw new Error("Homepage DynLex language client is not initialized");
  }
  if (!snippetLspDocument) {
    throw new Error("Homepage DynLex document is not initialized");
  }
  await snippetLspDocument.replaceText(sourceText);
  return snippetLspDocument.identifier;
}

function queueCompilerTask(task) {
  const result = snippetCompilerQueue.then(task);
  snippetCompilerQueue = result.then(
    () => undefined,
    () => undefined
  );
  return result;
}

function activeSnippetSource(sketch) {
  const visiblePanelSource = sketch.querySelector("[data-lab-panel]:not([hidden]) [data-snippet-source]");
  return visiblePanelSource || required("[data-snippet-source]", sketch);
}

function renderSemanticHighlight(source, tokenData, legend, stateName) {
  const highlightState = snippetHighlightStates.get(source);
  if (!highlightState) {
    throw new Error("Snippet highlighter has not been installed");
  }

  const sourceText = source.value;
  renderSemanticTokens(highlightState.code, sourceText, tokenData, legend, {
    baseClass: "snippet-token",
    classPrefix: "snippet-token-"
  });
  highlightState.shell.dataset.highlightState = stateName;
}

function syncSnippetHighlightScroll(source) {
  const highlightState = snippetHighlightStates.get(source);
  if (!highlightState) {
    throw new Error("Snippet highlighter has not been installed");
  }
  highlightState.shell.style.setProperty("--snippet-scroll-x", `${-source.scrollLeft}px`);
  highlightState.shell.style.setProperty("--snippet-scroll-y", `${-source.scrollTop}px`);
}

async function installSnippetHighlighter(source) {
  const cacheKey = await semanticHighlightKey(source.value);
  const cachedTokens = semanticHighlightCache.get(cacheKey);
  if (!cachedTokens) {
    throw new Error("Editable homepage snippet is missing from the generated highlight cache");
  }
  runtimeSemanticHighlightCache.set(source.value, cachedTokens);

  const shell = document.createElement("div");
  shell.className = "snippet-editor-shell";
  if (source.classList.contains("hero-snippet-editor")) {
    shell.classList.add("hero-snippet-editor-shell");
  }
  if (source.classList.contains("language-snippet-editor")) {
    shell.classList.add("language-snippet-editor-shell");
  }

  const highlight = document.createElement("pre");
  highlight.className = `${source.className} snippet-highlight`;
  highlight.setAttribute("aria-hidden", "true");
  const code = document.createElement("code");
  highlight.append(code);

  source.before(shell);
  shell.append(highlight, source);
  snippetHighlightStates.set(source, { shell, code, generation: 0, timer: null });
  renderSemanticHighlight(source, cachedTokens, semanticTokenLegend, "cached");
  source.addEventListener("scroll", () => syncSnippetHighlightScroll(source), { passive: true });
}

function scheduleSemanticHighlight(source) {
  const highlightState = snippetHighlightStates.get(source);
  if (!highlightState) {
    throw new Error("Snippet highlighter has not been installed");
  }

  highlightState.generation += 1;
  const generation = highlightState.generation;
  if (highlightState.timer !== null) {
    clearTimeout(highlightState.timer);
    highlightState.timer = null;
  }

  const cachedTokens = runtimeSemanticHighlightCache.get(source.value);
  renderSemanticHighlight(source, cachedTokens ?? [], semanticTokenLegend, cachedTokens ? "cached" : "loading");
  void ensureSnippetWorker().catch((error) => {
    if (generation !== highlightState.generation) return;
    console.error("Homepage syntax highlighter failed to initialize", error);
    highlightState.shell.dataset.highlightState = "error";
  });
  if (cachedTokens) return;

  highlightState.timer = setTimeout(() => {
    highlightState.timer = null;
    const sourceText = source.value;
    void queueCompilerTask(async () => {
      if (generation !== highlightState.generation || sourceText !== source.value) return;
      await ensureSnippetWorker();
      const textDocument = await syncSnippetLspDocument(sourceText);
      const response = await snippetLsp.request("textDocument/semanticTokens/full", {
        textDocument
      });
      if (generation !== highlightState.generation || sourceText !== source.value) return;
      const tokenData = Object.freeze([...response.data]);
      runtimeSemanticHighlightCache.set(sourceText, tokenData);
      renderSemanticHighlight(source, tokenData, semanticTokenLegend, "semantic");
    }).catch((error) => {
      if (generation !== highlightState.generation) return;
      console.error("Homepage syntax highlighting failed", error);
      highlightState.shell.dataset.highlightState = "error";
    });
  }, 160);
}

function renderSnippetDiagnostics(sketch, diagnostics) {
  const panel = required("[data-snippet-diagnostics]", sketch);
  panel.textContent = "";
  panel.hidden = diagnostics.length === 0;
  if (diagnostics.length === 0) {
    return;
  }
  panel.textContent = diagnostics.map((diagnostic) => {
    const severity = String(diagnostic.severity || "error").toUpperCase();
    const line = Number.isInteger(diagnostic.line) ? diagnostic.line : 1;
    const column = Number.isInteger(diagnostic.column) ? diagnostic.column : 1;
    return `${severity} ${line}:${column}  ${diagnostic.message}`;
  }).join("\n");
}

function renderSnippetOutput(sketch, stdout) {
  const output = required("[data-snippet-output]", sketch);
  const text = stdout.replace(/\r\n/g, "\n").replace(/\n$/, "");
  const mode = sketch.dataset.outputMode;

  if (mode === "number" || mode === "language") {
    output.textContent = text || "(no output)";
    return;
  }

  if (mode === "rhythm") {
    const lines = text.length > 0 ? text.split("\n") : ["(no output)"];
    output.replaceChildren(...lines.map((line) => {
      const span = document.createElement("span");
      span.textContent = line;
      return span;
    }));
    output.setAttribute("aria-label", `Output: ${lines.join(", ")}`);
    return;
  }

  if (mode === "color") {
    const words = (text || "no output").split(/\s+/);
    const strong = document.createElement("strong");
    strong.textContent = words.pop();
    const span = document.createElement("span");
    span.textContent = words.join(" ") || "output";
    output.replaceChildren(span, strong);
    return;
  }

  if (mode === "signal") {
    const signal = document.createElement("i");
    signal.setAttribute("aria-hidden", "true");
    const value = document.createElement("span");
    value.textContent = text || "(no output)";
    output.replaceChildren(signal, value);
    return;
  }

  throw new Error(`Unknown snippet output mode: ${mode}`);
}

function renderCompilationTime(milliseconds) {
  if (!Number.isFinite(milliseconds) || milliseconds < 0) {
    throw new Error("Compiler returned an invalid duration");
  }
  const display = milliseconds < 10 ? milliseconds.toFixed(1) : String(Math.round(milliseconds));
  compileMetric.textContent = `LAST COMPILE / ${display} MS`;
}

async function executeSketch(sketch) {
  const button = required("[data-snippet-run]", sketch);
  const source = activeSnippetSource(sketch);
  button.disabled = true;
  try {
    if (!snippetWorkerInitialized) {
      setSketchState(sketch, "loading");
    }
    await ensureSnippetWorker();
    setSketchState(sketch, "running");
    const compileResult = await callSnippetWorker("compile", {
      source: source.value,
      version: nextSnippetVersion++
    });
    renderCompilationTime(compileResult.compilationMilliseconds);
    renderSnippetDiagnostics(sketch, compileResult.diagnostics);
    if (compileResult.status !== 0) {
      setSketchState(sketch, "error");
      return;
    }

    const runResult = await callSnippetWorker("run");
    if (runResult.error) {
      console.error("Homepage sketch execution failed", runResult.error);
      throw new Error("Program execution failed");
    }
    renderSnippetOutput(sketch, runResult.stdout);
    source.dataset.edited = "false";
    setSketchState(sketch, "done");
  } catch (error) {
    console.error("Homepage sketch failed", error);
    const diagnostics = required("[data-snippet-diagnostics]", sketch);
    diagnostics.textContent = "An error occurred. Check the browser log.";
    diagnostics.hidden = false;
    setSketchState(sketch, "error");
  } finally {
    button.disabled = false;
  }
}

function queueSketch(sketch) {
  const button = required("[data-snippet-run]", sketch);
  button.disabled = true;
  setSketchState(sketch, "queued");
  void queueCompilerTask(() => executeSketch(sketch));
}

const snippetSources = runnableSketches.flatMap((sketch) => (
  [...sketch.querySelectorAll("[data-snippet-source]")]
));
await Promise.all(snippetSources.map((source) => installSnippetHighlighter(source)));

for (const sketch of runnableSketches) {
  const button = required("[data-snippet-run]", sketch);
  const status = required("[data-snippet-status]", sketch);
  status.setAttribute("role", "status");
  status.setAttribute("aria-live", "polite");
  button.addEventListener("click", () => queueSketch(sketch));

  for (const source of sketch.querySelectorAll("[data-snippet-source]")) {
    source.addEventListener("input", () => {
      source.dataset.edited = "true";
      scheduleSemanticHighlight(source);
      if (source === activeSnippetSource(sketch)) {
        setSketchState(sketch, "edited");
      }
    });
    source.addEventListener("keydown", (event) => {
      if (event.key === "Tab") {
        event.preventDefault();
        source.setRangeText("    ", source.selectionStart, source.selectionEnd, "end");
        source.dispatchEvent(new Event("input", { bubbles: true }));
      } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
        event.preventDefault();
        if (!button.disabled) {
          queueSketch(sketch);
        }
      }
    });
  }
}

const canvas = required("[data-lexicon-field]");
const context = canvas.getContext("2d");
if (!context) {
  throw new Error("DynLex homepage requires a 2D canvas context");
}

const graphNodes = [
  { x: 0.06, y: 0.19, label: "words", accent: "#c7ff38" },
  { x: 0.24, y: 0.08, label: "patterns" },
  { x: 0.42, y: 0.25, label: "meaning" },
  { x: 0.63, y: 0.11, label: "name" },
  { x: 0.82, y: 0.26, label: "reuse", accent: "#85dcff" },
  { x: 0.94, y: 0.08, label: "share" },
  { x: 0.14, y: 0.71, label: "read" },
  { x: 0.36, y: 0.86, label: "shape", accent: "#a58aff" },
  { x: 0.58, y: 0.68, label: "write" },
  { x: 0.79, y: 0.84, label: "explain" },
  { x: 0.96, y: 0.65, label: "build", accent: "#ff806d" }
];

const graphEdges = [
  [0, 1], [0, 6], [1, 2], [1, 7], [2, 3], [2, 7], [3, 4], [3, 8],
  [4, 5], [4, 8], [4, 9], [5, 10], [6, 7], [7, 8], [8, 9], [9, 10]
];

const pointer = { x: 0, y: 0, targetX: 0, targetY: 0 };
let canvasWidth = 0;
let canvasHeight = 0;
let pixelRatio = 1;
let frameHandle = 0;
let fieldActive = !document.hidden && window.scrollY < window.innerHeight * 1.25;

function resizeCanvas() {
  pixelRatio = Math.min(2, window.devicePixelRatio || 1);
  canvasWidth = window.innerWidth;
  canvasHeight = window.innerHeight;
  canvas.width = Math.round(canvasWidth * pixelRatio);
  canvas.height = Math.round(canvasHeight * pixelRatio);
  canvas.style.width = canvasWidth + "px";
  canvas.style.height = canvasHeight + "px";
  context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
}

function nodePosition(node, index, time) {
  const drift = reducedMotion.matches ? 0 : Math.sin(time * 0.00035 + index * 1.7) * 7;
  return {
    x: node.x * canvasWidth + pointer.x * (index % 3 + 1) * 0.9,
    y: node.y * canvasHeight + drift + pointer.y * (index % 2 + 1) * 0.65
  };
}

function drawField(time = 0) {
  context.clearRect(0, 0, canvasWidth, canvasHeight);
  pointer.x += (pointer.targetX - pointer.x) * 0.045;
  pointer.y += (pointer.targetY - pointer.y) * 0.045;
  const positions = graphNodes.map((node, index) => nodePosition(node, index, time));

  context.lineWidth = 1;
  for (const [from, to] of graphEdges) {
    context.beginPath();
    context.moveTo(positions[from].x, positions[from].y);
    context.lineTo(positions[to].x, positions[to].y);
    context.strokeStyle = "rgba(255, 255, 255, 0.09)";
    context.stroke();
  }

  context.font = '10px "DM Mono", monospace';
  context.textBaseline = "middle";
  for (let index = 0; index < graphNodes.length; index += 1) {
    const node = graphNodes[index];
    const position = positions[index];
    context.beginPath();
    context.arc(position.x, position.y, node.accent ? 3.5 : 2, 0, Math.PI * 2);
    context.fillStyle = node.accent || "rgba(255, 255, 255, 0.45)";
    context.fill();
    context.globalAlpha = node.accent ? 0.62 : 1;
    context.fillStyle = node.accent || "rgba(255, 255, 255, 0.22)";
    context.fillText(node.label.toUpperCase(), position.x + 10, position.y);
    context.globalAlpha = 1;
  }
}

function animateField(time) {
  if (reducedMotion.matches) {
    frameHandle = 0;
    drawField(time);
    return;
  }
  drawField(time);
  frameHandle = requestAnimationFrame(animateField);
}

function syncFieldMotion() {
  cancelAnimationFrame(frameHandle);
  frameHandle = 0;
  drawField(performance.now());
  if (!reducedMotion.matches && fieldActive) {
    frameHandle = requestAnimationFrame(animateField);
  }
}

function updateFieldActivity() {
  const nextActive = !document.hidden && window.scrollY < window.innerHeight * 1.25;
  if (nextActive === fieldActive) {
    return;
  }
  fieldActive = nextActive;
  if (fieldActive) {
    syncFieldMotion();
  } else {
    cancelAnimationFrame(frameHandle);
    frameHandle = 0;
  }
}

window.addEventListener("pointermove", (event) => {
  pointer.targetX = (event.clientX / window.innerWidth - 0.5) * 18;
  pointer.targetY = (event.clientY / window.innerHeight - 0.5) * 18;
}, { passive: true });

window.addEventListener("resize", () => {
  resizeCanvas();
  drawField(performance.now());
});
window.addEventListener("scroll", updateFieldActivity, { passive: true });
document.addEventListener("visibilitychange", updateFieldActivity);

reducedMotion.addEventListener("change", syncFieldMotion);
resizeCanvas();
syncFieldMotion();

window.addEventListener("pagehide", () => {
  cancelAnimationFrame(frameHandle);
  if (snippetLsp) {
    void queueCompilerTask(async () => {
      await shutdownLsp(snippetLsp);
      snippetWorker.terminate();
    }).catch((error) => {
      console.error("Homepage DynLex language server shutdown failed", error);
    });
  }
}, { once: true });
