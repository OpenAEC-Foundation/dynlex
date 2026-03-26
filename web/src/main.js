import * as monaco from "monaco-editor";
import "./styles.css";

self.MonacoEnvironment = {
  getWorker() {
    return new Worker(new URL("monaco-editor/esm/vs/editor/editor.worker.js", import.meta.url), {
      type: "module"
    });
  }
};

monaco.languages.register({ id: "dynlex" });
monaco.editor.defineTheme("dynlex-soft", {
  base: "vs",
  inherit: true,
  rules: [
    { token: "keyword", foreground: "0b6f95", fontStyle: "bold" },
    { token: "string", foreground: "915b0f" },
    { token: "comment", foreground: "6d8290" }
  ],
  colors: {
    "editor.background": "#fbfcfe",
    "editorLineNumber.foreground": "#8c96a3",
    "editorLineNumber.activeForeground": "#1a2330"
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

const statusPill = document.getElementById("status-pill");
const compileButton = document.getElementById("compile-button");
const runButton = document.getElementById("run-button");
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
  scrollBeyondLastLine: false
});
monaco.editor.setTheme("dynlex-soft");

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

async function runCompile(trigger) {
  if (!workerReady) {
    return;
  }

  setCompileBusy(true);
  setStatus(trigger === "live" ? "Compiling live..." : "Compiling...");
  try {
    const result = await callWorker("compile", { source: model.getValue() });
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
