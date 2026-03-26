import * as monaco from "monaco-editor";
import "./styles.css";

self.MonacoEnvironment = {
  getWorker() {
    return new Worker(new URL("monaco-editor/esm/vs/editor/editor.worker.js", import.meta.url), {
      type: "module"
    });
  }
};

const THEME_STORAGE_KEY = "dynlex-web-theme";
const LSP_SEMANTIC_LEGEND = {
  tokenTypes: [
    "function",
    "section",
    "variable",
    "comment",
    "patternDefinition",
    "number",
    "string",
    "intrinsic",
    "type",
    "keyword"
  ],
  tokenModifiers: ["definition", "constant"]
};

monaco.languages.register({ id: "dynlex" });
monaco.languages.setMonarchTokensProvider("dynlex", {
  defaultToken: "text",
  keywords: [
    "import",
    "function",
    "section",
    "replacement",
    "execute",
    "if",
    "else",
    "loop",
    "match",
    "case",
    "class",
    "members",
    "globals",
    "macro",
    "local",
    "open"
  ],
  tokenizer: {
    root: [
      [/#.*$/, "comment"],
      [/\"(?:[^\"\\\\]|\\\\.)*\"/, "string"],
      [/\b\d+(?:\.\d+)?\b/, "number"],
      [/[A-Za-z_][\w-]*/, { cases: { "@keywords": "keyword", "@default": "function" } }]
    ]
  }
});

monaco.editor.defineTheme("dynlex-light", {
  base: "vs",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: "0f6cb2", fontStyle: "bold" },
    { token: "string", foreground: "9a5a08" },
    { token: "comment", foreground: "697b8c", fontStyle: "italic" },
    { token: "function", foreground: "156ec2" },
    { token: "section", foreground: "0f8667", fontStyle: "bold" },
    { token: "variable", foreground: "273344" },
    { token: "number", foreground: "8f3e90" },
    { token: "type", foreground: "9d3d17" },
    { token: "intrinsic", foreground: "8a2da8", fontStyle: "bold" },
    { token: "patternDefinition", foreground: "136f9f", fontStyle: "bold" }
  ],
  colors: {
    "editor.background": "#f8fbff",
    "editor.foreground": "#1a2533",
    "editorLineNumber.foreground": "#8a97a6",
    "editorLineNumber.activeForeground": "#253549",
    "editorCursor.foreground": "#0f6cb2",
    "editor.selectionBackground": "#cfe7ff"
  }
});

monaco.editor.defineTheme("dynlex-dark", {
  base: "vs-dark",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: "62b0ff", fontStyle: "bold" },
    { token: "string", foreground: "f5ba5d" },
    { token: "comment", foreground: "7d90a3", fontStyle: "italic" },
    { token: "function", foreground: "72b8ff" },
    { token: "section", foreground: "56d4ad", fontStyle: "bold" },
    { token: "variable", foreground: "d7e3f2" },
    { token: "number", foreground: "f49cff" },
    { token: "type", foreground: "ff9a70" },
    { token: "intrinsic", foreground: "d29cff", fontStyle: "bold" },
    { token: "patternDefinition", foreground: "64d6ff", fontStyle: "bold" }
  ],
  colors: {
    "editor.background": "#101a2a",
    "editor.foreground": "#dce8f5",
    "editorLineNumber.foreground": "#607289",
    "editorLineNumber.activeForeground": "#9db8d5",
    "editorCursor.foreground": "#7dc1ff",
    "editor.selectionBackground": "#1f4264"
  }
});

const defaultSource = `import lib/std.dl

print "Hello from DynLex Web"
`;

const worker = new Worker(new URL("./worker/compilerWorker.js", import.meta.url), { type: "module" });
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

function toMonacoRange(range) {
  if (!range || !range.start || !range.end) {
    return null;
  }
  return {
    startLineNumber: (range.start.line ?? 0) + 1,
    startColumn: (range.start.character ?? 0) + 1,
    endLineNumber: (range.end.line ?? 0) + 1,
    endColumn: (range.end.character ?? 0) + 1
  };
}

function positionPayload(position) {
  return {
    line: Math.max(0, position.lineNumber - 1),
    column: Math.max(0, position.column - 1)
  };
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

const statusPill = document.getElementById("status-pill");
const compileButton = document.getElementById("compile-button");
const runButton = document.getElementById("run-button");
const themeButton = document.getElementById("theme-button");
const diagnosticsEmpty = document.getElementById("diagnostics-empty");
const diagnosticsList = document.getElementById("diagnostics-list");
const compilerLog = document.getElementById("compiler-log");
const runtimeOutput = document.getElementById("runtime-output");

const model = monaco.editor.createModel(defaultSource, "dynlex", monaco.Uri.parse("file:///workspace/main.dl"));
const editor = monaco.editor.create(document.getElementById("editor"), {
  model,
  minimap: { enabled: false },
  automaticLayout: true,
  fontFamily: "'IBM Plex Mono', Consolas, Menlo, monospace",
  fontSize: 14,
  lineHeight: 22,
  tabSize: 4,
  insertSpaces: true,
  scrollBeyondLastLine: false,
  "semanticHighlighting.enabled": true
});

function applyTheme(nextTheme) {
  const theme = normalizeTheme(nextTheme);
  document.documentElement.dataset.theme = theme;
  monaco.editor.setTheme(theme === "dark" ? "dynlex-dark" : "dynlex-light");
  if (themeButton) {
    themeButton.textContent = theme === "dark" ? "Light mode" : "Dark mode";
  }
  localStorage.setItem(THEME_STORAGE_KEY, theme);
}

applyTheme(getInitialTheme());

if (themeButton) {
  themeButton.addEventListener("click", () => {
    const current = document.documentElement.dataset.theme === "dark" ? "dark" : "light";
    applyTheme(current === "dark" ? "light" : "dark");
  });
}

function setStatus(text) {
  statusPill.textContent = text;
}

function setCompileBusy(isBusy) {
  compileButton.disabled = isBusy;
  compileButton.textContent = isBusy ? "Compiling..." : "Compile";
}

function diagnosticsToMarkers(diagnostics) {
  return diagnostics
    .filter((diagnostic) => Number.isInteger(diagnostic.line) && Number.isInteger(diagnostic.column))
    .map((diagnostic) => {
      const startLine = diagnostic.range?.start?.line ?? diagnostic.line;
      const startColumn = diagnostic.range?.start?.column ?? diagnostic.column;
      let endLine = diagnostic.range?.end?.line ?? startLine;
      let endColumn = diagnostic.range?.end?.column ?? (startColumn + 1);
      if (endLine < startLine) {
        endLine = startLine;
      }
      if (endLine === startLine && endColumn <= startColumn) {
        endColumn = startColumn + 1;
      }

      let severity = monaco.MarkerSeverity.Info;
      if (diagnostic.severity === "error") {
        severity = monaco.MarkerSeverity.Error;
      } else if (diagnostic.severity === "warning") {
        severity = monaco.MarkerSeverity.Warning;
      }

      return {
        severity,
        message: diagnostic.message,
        startLineNumber: startLine,
        startColumn,
        endLineNumber: endLine,
        endColumn
      };
    });
}

function renderDiagnostics(diagnostics) {
  diagnosticsList.innerHTML = "";
  const hasDiagnostics = diagnostics.length > 0;
  diagnosticsEmpty.style.display = hasDiagnostics ? "none" : "block";

  for (const diagnostic of diagnostics) {
    const item = document.createElement("li");
    item.className = `severity-${diagnostic.severity || "info"}`;
    item.textContent = diagnostic.message;

    const line = Number.isInteger(diagnostic.line) ? diagnostic.line : 1;
    const column = Number.isInteger(diagnostic.column) ? diagnostic.column : 1;
    const meta = document.createElement("span");
    meta.className = "diag-meta";
    meta.textContent = `${diagnostic.severity ?? "info"} at ${line}:${column}`;
    item.appendChild(meta);

    item.addEventListener("click", () => {
      editor.revealPositionInCenter({ lineNumber: line, column });
      editor.setPosition({ lineNumber: line, column });
      editor.focus();
    });

    diagnosticsList.appendChild(item);
  }

  monaco.editor.setModelMarkers(model, "dynlex", diagnosticsToMarkers(diagnostics));
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
    runtimeOutput.textContent = `Runtime error:\n${result.error}`;
    return;
  }
  runtimeOutput.textContent = result.stdout || "(no stdout)";
}

let compileTimer = null;
let workerReady = false;

monaco.languages.registerDefinitionProvider("dynlex", {
  async provideDefinition(currentModel, position, cancellationToken) {
    if (!workerReady) {
      return null;
    }
    try {
      const response = await callWorker("lsp.definition", {
        source: currentModel.getValue(),
        version: currentModel.getVersionId(),
        ...positionPayload(position)
      });
      if (cancellationToken?.isCancellationRequested || !response || !response.range || !response.uri) {
        return null;
      }
      return {
        uri: monaco.Uri.parse(response.uri),
        range: toMonacoRange(response.range)
      };
    } catch {
      return null;
    }
  }
});

monaco.languages.registerHoverProvider("dynlex", {
  async provideHover(currentModel, position, cancellationToken) {
    if (!workerReady) {
      return null;
    }
    try {
      const response = await callWorker("lsp.hover", {
        source: currentModel.getValue(),
        version: currentModel.getVersionId(),
        ...positionPayload(position)
      });
      if (cancellationToken?.isCancellationRequested || !response) {
        return null;
      }

      const contents = [];
      if (typeof response.contents === "string") {
        contents.push({ value: response.contents });
      } else if (response.contents && typeof response.contents.value === "string") {
        contents.push({ value: response.contents.value });
      } else if (response.contents && typeof response.contents === "object") {
        contents.push({ value: JSON.stringify(response.contents, null, 2) });
      }

      if (contents.length === 0) {
        return null;
      }

      return {
        range: toMonacoRange(response.range),
        contents
      };
    } catch {
      return null;
    }
  }
});

monaco.languages.registerDocumentSemanticTokensProvider("dynlex", {
  getLegend() {
    return LSP_SEMANTIC_LEGEND;
  },

  async provideDocumentSemanticTokens(currentModel, _lastResultId, cancellationToken) {
    if (!workerReady) {
      return { data: new Uint32Array(0), resultId: String(currentModel.getVersionId()) };
    }

    try {
      const response = await callWorker("lsp.semanticTokens", {
        source: currentModel.getValue(),
        version: currentModel.getVersionId()
      });
      if (cancellationToken?.isCancellationRequested) {
        return { data: new Uint32Array(0), resultId: String(currentModel.getVersionId()) };
      }

      const tokenData = Array.isArray(response?.data) ? response.data : [];
      return {
        data: Uint32Array.from(tokenData.map((value) => (Number.isFinite(value) ? value : 0))),
        resultId: String(currentModel.getVersionId())
      };
    } catch {
      return { data: new Uint32Array(0), resultId: String(currentModel.getVersionId()) };
    }
  },

  releaseDocumentSemanticTokens() {}
});

async function runCompile(trigger) {
  if (!workerReady) {
    return;
  }

  setCompileBusy(true);
  setStatus(trigger === "live" ? "Compiling live..." : "Compiling...");
  try {
    const result = await callWorker("compile", {
      source: model.getValue(),
      version: model.getVersionId()
    });
    renderDiagnostics(result.diagnostics);
    renderCompilerLogMessages(result.compilerLog);
    runButton.disabled = !result.hasArtifact;
    if (result.status === 0) {
      setStatus(`Compiled #${result.artifactVersion}`);
    } else {
      setStatus("Compile reported diagnostics");
    }
  } catch (error) {
    renderCompilerLogMessages([{ level: "error", message: error.message }]);
    setStatus("Compile failed");
  } finally {
    setCompileBusy(false);
  }
}

async function runProgram() {
  if (runButton.disabled) {
    return;
  }
  setStatus("Running...");
  try {
    const result = await callWorker("run");
    renderRuntime(result);
    setStatus(result.error ? "Run failed" : "Run complete");
  } catch (error) {
    renderRuntime({ error: error.message, stdout: "" });
    setStatus("Run failed");
  }
}

compileButton.addEventListener("click", () => {
  runCompile("manual");
});

runButton.addEventListener("click", () => {
  runProgram();
});

model.onDidChangeContent(() => {
  if (compileTimer) {
    clearTimeout(compileTimer);
  }
  compileTimer = setTimeout(() => runCompile("live"), 450);
});

(async () => {
  try {
    await callWorker("init");
    workerReady = true;
    setStatus("Ready");
    await runCompile("manual");
  } catch (error) {
    setStatus("Init failed");
    renderCompilerLogMessages([{ level: "error", message: error.message }]);
  }
})();
