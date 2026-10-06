import * as monaco from "monaco-editor/editor";
import "monaco-editor/features/clipboard/register";
import "monaco-editor/features/codeAction/register";
import "monaco-editor/features/codicon/register";
import "monaco-editor/features/contextmenu/register";
import "monaco-editor/features/documentSymbols/register";
import "monaco-editor/features/find/register";
import "monaco-editor/features/gotoSymbol/register";
import "monaco-editor/features/hover/register";
import "monaco-editor/features/readOnlyMessage/register";
import "monaco-editor/features/semanticTokens/register";
import "monaco-editor/editor/contrib/semanticTokens/browser/documentSemanticTokens";
import "monaco-editor/editor/contrib/suggest/browser/suggestController";
import { DynLexLanguageFeatures } from "./lspIntegration.js";
import callColors from "../../../../shared/call-colors.json";
export { DynLexConnection } from "./languageConnection.js";
export { monaco };
import "./editor.css";

self.MonacoEnvironment = {
  getWorker() {
    return new Worker(new URL("monaco-editor/editor/editor.worker.js", import.meta.url), {
      type: "module"
    });
  }
};

monaco.languages.register({ id: "dynlex" });

const scrollbarThemeColors = Object.freeze({
  "scrollbarSlider.background": "#3F474199",
  "scrollbarSlider.hoverBackground": "#59625BCC",
  "scrollbarSlider.activeBackground": "#707A72E6"
});

const callColorRules = theme => callColors.flatMap((color, depth) =>
  ["function", "intrinsic", "type"].map(type => ({ token: `${type}.callDepth${depth}`, foreground: color[theme] })));

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
    { token: "patternDefinition", foreground: "2E5797", fontStyle: "bold" },
    ...callColorRules("light")
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
    { token: "patternDefinition", foreground: "C9FF38", fontStyle: "bold" },
    ...callColorRules("dark")
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

monaco.languages.setLanguageConfiguration("dynlex", {
  comments: { lineComment: "#" },
  indentationRules: { increaseIndentPattern: /^(?:[^"#]|"(?:\\.|[^"\\])*")*:\s*(?:#.*)?$/ }
});

let nextDocument = 1;
export function createDynLexEditor(host, { value = "", uri, embedded = false, onChange, onRun, ...options } = {}) {
  const model = monaco.editor.createModel(value, "dynlex", monaco.Uri.parse(uri ?? `file:///workspace/editor-${nextDocument++}.dl`));
  const editor = monaco.editor.create(host, {
    model, theme: "dynlex-dark", minimap: { enabled: false }, automaticLayout: true,
    fontFamily: "'DM Mono', Consolas, Menlo, monospace", fontSize: 14, lineHeight: 23,
    tabSize: 4, insertSpaces: true, autoIndent: "full", scrollBeyondLastLine: false, padding: { top: 14, bottom: 14 },
    renderLineHighlight: "line", smoothScrolling: true, "semanticHighlighting.enabled": true,
    tabCompletion: "on", acceptSuggestionOnEnter: "off", wordBasedSuggestions: "off",
    quickSuggestions: { other: "on", comments: "off", strings: "off" },
    suggest: { showWords: false, preview: false }, fixedOverflowWidgets: true,
    ...(embedded ? { fontSize: 12, lineHeight: 22, lineNumbers: "off", folding: false, glyphMargin: false, lineDecorationsWidth: 8 } : {}),
    ...options
  });
  let features = null, connecting = null, disposed = false, settingValue = false;
  const changeListener = model.onDidChangeContent(() => { if (!settingValue) onChange?.(model.getValue()); });
  const decorations = editor.createDecorationsCollection();
  if (onRun) editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, onRun);
  host.dataset.editorReady = "true";
  return {
    editor, model,
    getValue: () => model.getValue(),
    setValue(value) { settingValue = true;try { model.setValue(value); } finally { settingValue = false; } },
    focus: () => editor.focus(),
    onChange: listener => model.onDidChangeContent(() => { if (!settingValue) listener(model.getValue()); }),
    async connect(options) {
      features = new DynLexLanguageFeatures({ monaco, editor, mainModel: model, embedded, ...options });
      connecting = features.start();
      await connecting;
      if (disposed) return;
      host.dataset.languageReady = "true";
      return features;
    },
    callExpressions: () => features.callExpressions(),
    decorate(range = null, state = "active") {
      decorations.set(range ? [{ range, options: { className: `dynlex-code-${state}` } }] : []);
    },
    dispose() {
      disposed = true; delete host.dataset.editorReady; delete host.dataset.languageReady;
      changeListener.dispose(); decorations.clear();
      const cleanup = async () => {
        try { if (connecting) { await connecting; await features.stop(); } }
        catch (error) { console.error("Editor shutdown failed", error); }
        finally { editor.dispose(); model.dispose(); }
      };
      return cleanup();
    }
  };
}
