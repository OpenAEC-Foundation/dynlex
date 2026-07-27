import * as monaco from "monaco-editor/esm/vs/editor/editor.api.js";
import "monaco-editor/esm/vs/base/browser/ui/codicons/codiconStyles.js";
import "monaco-editor/esm/vs/editor/contrib/clipboard/browser/clipboard.js";
import "monaco-editor/esm/vs/editor/contrib/codeAction/browser/codeActionContributions.js";
import "monaco-editor/esm/vs/editor/contrib/contextmenu/browser/contextmenu.js";
import "monaco-editor/esm/vs/editor/contrib/documentSymbols/browser/documentSymbols.js";
import "monaco-editor/esm/vs/editor/contrib/find/browser/findController.js";
import "monaco-editor/esm/vs/editor/contrib/gotoSymbol/browser/goToCommands.js";
import "monaco-editor/esm/vs/editor/contrib/gotoSymbol/browser/link/goToDefinitionAtPosition.js";
import "monaco-editor/esm/vs/editor/contrib/hover/browser/hoverContribution.js";
import "monaco-editor/esm/vs/editor/contrib/readOnlyMessage/browser/contribution.js";
import "monaco-editor/esm/vs/editor/contrib/semanticTokens/browser/documentSemanticTokens.js";
import "monaco-editor/esm/vs/editor/contrib/semanticTokens/browser/viewportSemanticTokens.js";
import "monaco-editor/esm/vs/editor/contrib/suggest/browser/suggestController.js";
import { createShaderPreview } from "../../../../web/shader-renderer.js";
import { DynLexLanguageFeatures } from "./lspIntegration.js";
import "./styles.css";

self.MonacoEnvironment = {
  getWorker() {
    return new Worker(new URL("monaco-editor/esm/vs/editor/editor.worker.js", import.meta.url), {
      type: "module"
    });
  }
};

const THEME_STORAGE_KEY = "dynlex-web-theme";

monaco.languages.register({ id: "dynlex" });

const scrollbarThemeColors = Object.freeze({
  "scrollbarSlider.background": "#3F474199",
  "scrollbarSlider.hoverBackground": "#59625BCC",
  "scrollbarSlider.activeBackground": "#707A72E6"
});

monaco.editor.defineTheme("dynlex-light", {
  base: "vs",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: "C53A30", fontStyle: "bold" },
    { token: "string", foreground: "8A5A00" },
    { token: "comment", foreground: "74766F", fontStyle: "italic" },
    { token: "function", foreground: "304FC3" },
    { token: "section", foreground: "8A5A00", fontStyle: "bold" },
    { token: "variable", foreground: "1C211E" },
    { token: "number", foreground: "5C49B5" },
    { token: "type", foreground: "A33A27" },
    { token: "intrinsic", foreground: "6543B6", fontStyle: "bold" },
    { token: "patternDefinition", foreground: "2E5797", fontStyle: "bold" }
  ],
  colors: {
    ...scrollbarThemeColors,
    "editor.background": "#F8F6EF",
    "editor.foreground": "#1C211E",
    "editorLineNumber.foreground": "#A19F96",
    "editorLineNumber.activeForeground": "#30352F",
    "editorCursor.foreground": "#304FC3",
    "editor.selectionBackground": "#D9DDFF",
    "editor.lineHighlightBackground": "#F0EDE4",
    "editorIndentGuide.background1": "#DDD9CF"
  }
});

monaco.editor.defineTheme("dynlex-dark", {
  base: "vs-dark",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: "FF8B73", fontStyle: "bold" },
    { token: "string", foreground: "FFD787" },
    { token: "comment", foreground: "7F8B80", fontStyle: "italic" },
    { token: "function", foreground: "B8E5FF" },
    { token: "section", foreground: "FFD787", fontStyle: "bold" },
    { token: "variable", foreground: "E2E6DF" },
    { token: "number", foreground: "9AA5FF" },
    { token: "type", foreground: "FFAD9C" },
    { token: "intrinsic", foreground: "BFB1FF", fontStyle: "bold" },
    { token: "patternDefinition", foreground: "C9FF38", fontStyle: "bold" }
  ],
  colors: {
    ...scrollbarThemeColors,
    "editor.background": "#151816",
    "editor.foreground": "#E2E6DF",
    "editorLineNumber.foreground": "#555C56",
    "editorLineNumber.activeForeground": "#A9B0AA",
    "editorCursor.foreground": "#C9FF38",
    "editor.selectionBackground": "#38453A",
    "editor.lineHighlightBackground": "#1A1E1B",
    "editorIndentGuide.background1": "#2B302C"
  }
});

const defaultSource = `import lib/std.dl

function square value:
    replacement:
        value * value

print square 8 as line
`;

const queryParams = new URLSearchParams(window.location.search);
const shaderMode = queryParams.get("mode") === "shader";

async function loadRequestedShaderScene() {
  const requestedScene = queryParams.get("scene");
  if (requestedScene === null) {
    return null;
  }
  if (!shaderMode || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(requestedScene)) {
    throw new Error("Invalid shader scene");
  }

  const response = await fetch("/shaders/manifest.json");
  if (!response.ok) {
    throw new Error("Shader manifest could not be loaded");
  }
  const manifest = await response.json();
  const scene = manifest.scenes.find((candidate) => candidate.id === requestedScene);
  if (!scene) {
    throw new Error("Shader scene does not exist");
  }
  return scene;
}

const startupScene = await loadRequestedShaderScene();

function startupFileName() {
  if (startupScene !== null) {
    return `${startupScene.id}.dl`;
  }
  const requestedName = queryParams.get("name");
  if (requestedName && /^[a-zA-Z0-9][a-zA-Z0-9._-]*\.dl$/.test(requestedName)) {
    return requestedName;
  }
  return shaderMode ? "shader.dl" : "main.dl";
}

function decodeBase64Url(value) {
  const normalized = value.replace(/-/g, "+").replace(/_/g, "/");
  const paddingLength = (4 - (normalized.length % 4)) % 4;
  const padded = normalized + "=".repeat(paddingLength);

  try {
    const binary = atob(padded);
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  } catch {
    return null;
  }
}

function getStartupSource() {
  if (startupScene !== null) {
    return startupScene.source;
  }
  const encoded = queryParams.get("code64");
  if (typeof encoded === "string" && encoded.length > 0) {
    const decoded = decodeBase64Url(encoded);
    if (decoded !== null) {
      return decoded;
    }
  }

  const plain = queryParams.get("code");
  if (typeof plain === "string" && plain.length > 0) {
    return plain;
  }

  return defaultSource;
}

function shouldAutoRunOnStartup() {
  const value = queryParams.get("autorun");
  return value === "1" || value === "true";
}

async function loadShaderRenderer(config) {
  if (config === null) {
    return null;
  }
  const response = await fetch(`/${config.geometry.path}`);
  if (!response.ok) {
    throw new Error("Shader geometry could not be loaded");
  }
  const data = await response.arrayBuffer();
  const expectedBytes = config.geometry.vertexCount * 4 * Float32Array.BYTES_PER_ELEMENT;
  if (data.byteLength !== expectedBytes) {
    throw new Error("Shader geometry size does not match its renderer configuration");
  }
  return Object.freeze({
    geometry: Object.freeze({ ...config.geometry, data })
  });
}

const startupSource = getStartupSource();
const autoRunOnStartup = shouldAutoRunOnStartup();
const shaderRendererConfig = startupScene?.geometry
  ? Object.freeze({ geometry: Object.freeze({ ...startupScene.geometry }) })
  : null;
const fileName = startupFileName();
document.documentElement.dataset.workspaceMode = shaderMode ? "shader" : "program";
for (const fileLabel of document.querySelectorAll("[data-current-file]")) {
  fileLabel.textContent = fileName;
}
for (const workspaceKind of document.querySelectorAll("[data-workspace-kind]")) {
  workspaceKind.textContent = shaderMode ? "SHADER" : workspaceKind.textContent;
}

const worker = new Worker("/compiler/compiler-worker.js", { type: "module" });
let nextRequestId = 1;
const pendingRequests = new Map();

worker.onmessage = (event) => {
  const message = event.data;
  if (!message || typeof message.id !== "number") {
    return;
  }
  const pending = pendingRequests.get(message.id);
  if (!pending) {
    return;
  }
  pendingRequests.delete(message.id);
  if (message.ok) {
    pending.resolve(message.payload);
  } else {
    pending.reject(new Error(message.error || "Worker request failed"));
  }
};

function callWorker(type, payload = {}) {
  const id = nextRequestId++;
  return new Promise((resolve, reject) => {
    pendingRequests.set(id, { resolve, reject });
    worker.postMessage({ id, type, payload });
  });
}

function normalizeTheme(themeName) {
  return themeName === "dark" ? "dark" : "light";
}

function getInitialTheme() {
  const persisted = localStorage.getItem(THEME_STORAGE_KEY);
  if (persisted === "dark" || persisted === "light") {
    return persisted;
  }
  return window.matchMedia?.("(prefers-color-scheme: dark)")?.matches ? "dark" : "light";
}

function requiredElement(id) {
  const element = document.getElementById(id);
  if (!element) {
    throw new Error(`Missing required IDE element: #${id}`);
  }
  return element;
}

const statusPill = requiredElement("status-pill");
const statusText = requiredElement("status-text");
const runButton = requiredElement("run-button");
const themeButton = requiredElement("theme-button");
const diagnosticsEmpty = requiredElement("diagnostics-empty");
const diagnosticsList = requiredElement("diagnostics-list");
const diagnosticsCount = requiredElement("diagnostics-count");
const compilerLog = requiredElement("compiler-log");
const runtimeOutput = requiredElement("runtime-output");
const shaderPreviewShell = requiredElement("shader-preview-shell");
const shaderPreviewCanvas = requiredElement("shader-preview");
const editorElement = requiredElement("editor");
const projectFiles = requiredElement("project-files");
const toolTabs = [...document.querySelectorAll("[data-tool-tab]")];
const toolPanels = [...document.querySelectorAll("[data-tool-panel]")];

if (toolTabs.length !== 3 || toolPanels.length !== 3) {
  throw new Error("The IDE tool switcher must contain exactly three tabs and panels");
}

function selectToolTab(name, focus = false) {
  const selectedTab = toolTabs.find((tab) => tab.dataset.toolTab === name);
  const selectedPanel = toolPanels.find((panel) => panel.dataset.toolPanel === name);
  if (!selectedTab || !selectedPanel) {
    throw new Error(`Unknown IDE tool tab: ${name}`);
  }

  for (const tab of toolTabs) {
    const selected = tab === selectedTab;
    tab.setAttribute("aria-selected", String(selected));
    tab.tabIndex = selected ? 0 : -1;
  }
  for (const panel of toolPanels) {
    panel.hidden = panel !== selectedPanel;
  }

  if (focus) {
    selectedTab.focus();
  }
}

for (const [index, tab] of toolTabs.entries()) {
  tab.addEventListener("click", () => selectToolTab(tab.dataset.toolTab));
  tab.addEventListener("keydown", (event) => {
    let nextIndex = null;
    if (event.key === "ArrowRight") {
      nextIndex = (index + 1) % toolTabs.length;
    } else if (event.key === "ArrowLeft") {
      nextIndex = (index - 1 + toolTabs.length) % toolTabs.length;
    } else if (event.key === "Home") {
      nextIndex = 0;
    } else if (event.key === "End") {
      nextIndex = toolTabs.length - 1;
    }

    if (nextIndex !== null) {
      event.preventDefault();
      selectToolTab(toolTabs[nextIndex].dataset.toolTab, true);
    }
  });
}

const model = monaco.editor.createModel(
  startupSource,
  "dynlex",
  monaco.Uri.parse(`file:///workspace/${fileName}`)
);
const editor = monaco.editor.create(editorElement, {
  model,
  minimap: { enabled: false },
  automaticLayout: true,
  fontFamily: "'DM Mono', Consolas, Menlo, monospace",
  fontSize: 14,
  lineHeight: 23,
  tabSize: 4,
  insertSpaces: true,
  scrollBeyondLastLine: false,
  padding: { top: 18, bottom: 18 },
  renderLineHighlight: "line",
  smoothScrolling: true,
  "semanticHighlighting.enabled": true
});

function applyTheme(nextTheme) {
  const theme = normalizeTheme(nextTheme);
  document.documentElement.dataset.theme = theme;
  monaco.editor.setTheme(theme === "dark" ? "dynlex-dark" : "dynlex-light");
  themeButton.textContent = theme === "dark" ? "Light" : "Dark";
  themeButton.setAttribute("aria-label", `Switch to ${theme === "dark" ? "light" : "dark"} theme`);
  localStorage.setItem(THEME_STORAGE_KEY, theme);
}

applyTheme(getInitialTheme());

themeButton.addEventListener("click", () => {
  const current = document.documentElement.dataset.theme === "dark" ? "dark" : "light";
  applyTheme(current === "dark" ? "light" : "dark");
});

function setStatus(text, tone = "ready") {
  statusText.textContent = text;
  statusPill.dataset.tone = tone;
}

const diagnosticLevels = new Map([
  [1, "error"],
  [2, "warning"],
  [3, "info"],
  [4, "hint"]
]);

function diagnosticLevel(severity) {
  return diagnosticLevels.get(severity) ?? "info";
}

function renderDiagnostics(diagnostics) {
  diagnosticsList.innerHTML = "";
  const hasDiagnostics = diagnostics.length > 0;
  diagnosticsEmpty.hidden = hasDiagnostics;
  diagnosticsCount.textContent = String(diagnostics.length);

  for (const diagnostic of diagnostics) {
    const severity = diagnosticLevel(diagnostic.severity);
    const item = document.createElement("li");
    item.className = `severity-${severity}`;
    item.textContent = diagnostic.message;

    const line = diagnostic.range.start.line + 1;
    const column = diagnostic.range.start.character + 1;
    const meta = document.createElement("span");
    meta.className = "diag-meta";
    meta.textContent = `${severity} at ${line}:${column}`;
    item.appendChild(meta);

    item.addEventListener("click", async () => {
      await languageFeatures.showDocument(model.uri.toString());
      editor.revealPositionInCenter({ lineNumber: line, column });
      editor.setPosition({ lineNumber: line, column });
      editor.focus();
    });

    diagnosticsList.appendChild(item);
  }
}

function renderCompilerLogMessages(messages) {
  if (!messages.length) {
    compilerLog.textContent = "";
    return;
  }
  compilerLog.textContent = messages.map((entry) => `[${entry.level}] ${entry.message}`).join("\n");
}

function renderRuntime(result) {
  if (result.error) {
    console.error("DynLex program reported a runtime error", result.error);
    runtimeOutput.textContent = "An error occurred. Check the browser log.";
    return;
  }
  runtimeOutput.textContent = result.stdout || "(no stdout)";
}

let compileTimer = null;
let workerReady = false;
let shaderPreview = null;
let shaderRenderer = null;
let latestMainDiagnostics = [];
let languageFeatures = null;
let showingDiagnosticStatus = false;
let openSourceModels = [];

function documentName(documentModel) {
  return documentModel.uri.path.split("/").pop();
}

function renderOpenFiles(openModels) {
  projectFiles.innerHTML = "";
  const currentModel = editor.getModel();
  for (const openModel of openModels) {
    const listItem = document.createElement("div");
    listItem.setAttribute("role", "listitem");
    const item = document.createElement("button");
    item.className = `file-item${openModel === currentModel ? " active" : ""}`;
    item.type = "button";
    item.setAttribute("aria-current", openModel === currentModel ? "page" : "false");

    const icon = document.createElement("i");
    icon.setAttribute("aria-hidden", "true");
    icon.textContent = "D";
    const label = document.createElement("span");
    label.dataset.fileUri = openModel.uri.toString();
    label.textContent = documentName(openModel);
    const state = document.createElement("b");
    state.setAttribute("aria-hidden", "true");
    state.textContent = openModel === model ? "●" : "◇";

    item.append(icon, label, state);
    item.addEventListener("click", () => {
      void languageFeatures.showDocument(openModel.uri.toString()).catch((error) => {
        console.error("Could not activate DynLex document", error);
      });
    });
    listItem.appendChild(item);
    projectFiles.appendChild(listItem);
  }
}

function showDiagnosticStatus() {
  if (latestMainDiagnostics.length === 0) {
    setStatus("Ready");
    showingDiagnosticStatus = false;
    return;
  }
  const hasErrors = latestMainDiagnostics.some((diagnostic) => diagnostic.severity === 1);
  if (hasErrors) {
    selectToolTab("feedback");
  }
  setStatus(
    `${latestMainDiagnostics.length} ${latestMainDiagnostics.length === 1 ? "problem" : "problems"}`,
    hasErrors ? "error" : "ready"
  );
  showingDiagnosticStatus = true;
}

async function runCompile() {
  if (!workerReady) {
    return;
  }

  const sourceVersion = model.getVersionId();
  runButton.disabled = true;
  setStatus("Analyzing…", "busy");
  try {
    const result = await callWorker(shaderMode ? "compile.shader" : "compile", {
      source: model.getValue(),
      version: sourceVersion,
      renderer: shaderRenderer !== null
    });
    if (sourceVersion !== model.getVersionId()) {
      return;
    }
    renderCompilerLogMessages(result.compilerLog);
    runButton.disabled = shaderMode ? false : !result.hasArtifact;
    if (result.status === 0) {
      if (shaderMode) {
        try {
          shaderPreview.replaceProgram({
            fragmentSource: result.fragmentSource,
            ...(shaderRenderer
              ? {
                  vertexSource: result.vertexSource,
                  geometry: shaderRenderer.geometry
                }
              : {})
          }, result.uniforms);
          selectToolTab("output");
        } catch (error) {
          console.error("Live shader preview update failed", error);
          renderCompilerLogMessages([{ level: "error", message: "An error occurred. Check the browser log." }]);
          selectToolTab("activity");
          setStatus("Preview failed", "error");
          return;
        }
      }
      showDiagnosticStatus();
    } else {
      if (latestMainDiagnostics.length === 0) {
        setStatus("Build failed", "error");
      } else {
        showDiagnosticStatus();
      }
    }
  } catch (error) {
    if (sourceVersion !== model.getVersionId()) {
      return;
    }
    console.error("Code analysis failed", error);
    renderCompilerLogMessages([{ level: "error", message: "An error occurred. Check the browser log." }]);
    selectToolTab("activity");
    setStatus("Analysis failed", "error");
  }
}

async function runProgram() {
  if (runButton.disabled) {
    return;
  }
  selectToolTab("output");
  setStatus("Running…", "busy");
  try {
    const result = await callWorker("run");
    renderRuntime(result);
    setStatus(result.error ? "Run failed" : "Finished", result.error ? "error" : "ready");
  } catch (error) {
    console.error("Program run failed", error);
    renderRuntime({ error: "An error occurred. Check the browser log.", stdout: "" });
    setStatus("Run failed", "error");
  }
}

runButton.addEventListener("click", () => {
  if (shaderMode) {
    runCompile();
  } else {
    runProgram();
  }
});

editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => {
  if (runButton.disabled) return;
  if (shaderMode) runCompile();
  else runProgram();
});

model.onDidChangeContent(() => {
  runButton.disabled = true;
  if (compileTimer) {
    clearTimeout(compileTimer);
  }
  compileTimer = setTimeout(() => runCompile(), 300);
});

(async () => {
  try {
    if (shaderMode) {
      shaderPreviewShell.hidden = false;
      runtimeOutput.hidden = true;
      requiredElement("tool-tab-output").textContent = "Preview";
      document.querySelector("[data-output-kicker]").textContent = "SUCCESSFUL BUILD";
      document.querySelector("[data-output-title]").textContent = "Live preview";
      requiredElement("run-button").querySelector("[data-run-label]").textContent = "Recompile";
      requiredElement("run-button").querySelector("[data-run-icon]").textContent = "↻";
      shaderRenderer = await loadShaderRenderer(shaderRendererConfig);
      shaderPreview = createShaderPreview(shaderPreviewCanvas);
    }
    await callWorker("init");
    workerReady = true;
    languageFeatures = new DynLexLanguageFeatures({
      monaco,
      editor,
      mainModel: model,
      exchange: (message) => callWorker("lsp.exchange", { message }),
      analysisProfiles: shaderMode
        ? [
            { target: "spirv", shaderStage: "fragment" },
            ...(shaderRendererConfig === null
              ? []
              : [{ target: "spirv", shaderStage: "vertex" }])
          ]
        : [{ target: "cpu" }],
      onDiagnostics(uri, diagnostics) {
        if (uri !== model.uri.toString()) {
          return;
        }
        latestMainDiagnostics = diagnostics;
        renderDiagnostics(diagnostics);
        if (statusPill.dataset.tone === "busy") {
          return;
        }
        if (diagnostics.length > 0) {
          showDiagnosticStatus();
        } else if (showingDiagnosticStatus) {
          showDiagnosticStatus();
        }
      },
      onModelChanged(currentModel) {
        const currentName = currentModel?.uri.path.split("/").pop() ?? fileName;
        for (const fileLabel of document.querySelectorAll("[data-current-file]")) {
          fileLabel.textContent = currentName;
        }
        renderOpenFiles(openSourceModels);
      },
      onDocumentsChanged(openModels) {
        openSourceModels = openModels;
        renderOpenFiles(openSourceModels);
      }
    });
    await languageFeatures.start();
    await runCompile();
    if (autoRunOnStartup && !shaderMode) {
      await runProgram();
    }
  } catch (error) {
    console.error("IDE initialization failed", error);
    selectToolTab("activity");
    setStatus("Could not start", "error");
    renderCompilerLogMessages([{ level: "error", message: "An error occurred. Check the browser log." }]);
  }
})();

window.addEventListener("pagehide", () => {
  if (languageFeatures) {
    void languageFeatures.stop().catch((error) => {
      console.error("DynLex language server shutdown failed", error);
    });
  }
}, { once: true });
