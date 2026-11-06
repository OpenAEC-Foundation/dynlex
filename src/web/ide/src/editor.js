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

const themeTokens = getComputedStyle(document.documentElement);
const themeColor = (name) => themeTokens.getPropertyValue(`--${name}`).trim();
const tokenColor = (name) => themeColor(name).slice(1);
const scrollbarThemeColors = Object.freeze({
  "scrollbarSlider.background": "#64646F99",
  "scrollbarSlider.hoverBackground": "#85858FCC",
  "scrollbarSlider.activeBackground": "#A1A1AAE6"
});

const callColorRules = theme => callColors.flatMap((color, depth) =>
  ["function", "intrinsic", "type"].map(type => ({ token: `${type}.callDepth${depth}`, foreground: color[theme] })));

monaco.editor.defineTheme("dynlex-light", {
  base: "vs",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: tokenColor("accent-on-light"), fontStyle: "bold" },
    { token: "string", foreground: tokenColor("success-on-light") },
    { token: "comment", foreground: tokenColor("muted-on-light"), fontStyle: "italic" },
    { token: "function", foreground: tokenColor("accent-on-light") },
    { token: "section", foreground: tokenColor("accent-on-light"), fontStyle: "bold" },
    { token: "variable", foreground: tokenColor("deep-forge") },
    { token: "number", foreground: tokenColor("accent-on-light") },
    { token: "type", foreground: tokenColor("accent-on-light") },
    { token: "intrinsic", foreground: tokenColor("accent-on-light"), fontStyle: "bold" },
    { token: "patternDefinition", foreground: tokenColor("accent-on-light"), fontStyle: "bold" },
    ...callColorRules("light")
  ],
  colors: {
    ...scrollbarThemeColors,
    "editor.background": themeColor("blueprint-white"),
    "editor.foreground": themeColor("deep-forge"),
    "editorLineNumber.foreground": themeColor("muted-on-light"),
    "editorLineNumber.activeForeground": themeColor("deep-forge"),
    "editorCursor.foreground": themeColor("accent-on-light"),
    "editor.selectionBackground": "#D9770633",
    "editor.lineHighlightBackground": themeColor("concrete"),
    "editorIndentGuide.background1": "#D6D3D1"
  }
});

monaco.editor.defineTheme("dynlex-dark", {
  base: "vs-dark",
  inherit: true,
  semanticHighlighting: true,
  rules: [
    { token: "keyword", foreground: tokenColor("warm-gold"), fontStyle: "bold" },
    { token: "string", foreground: tokenColor("success-on-dark") },
    { token: "comment", foreground: tokenColor("scaffold-gray"), fontStyle: "italic" },
    { token: "function", foreground: tokenColor("warm-gold") },
    { token: "section", foreground: tokenColor("warm-gold"), fontStyle: "bold" },
    { token: "variable", foreground: tokenColor("blueprint-white") },
    { token: "number", foreground: tokenColor("warm-gold") },
    { token: "type", foreground: tokenColor("warm-gold") },
    { token: "intrinsic", foreground: tokenColor("warm-gold"), fontStyle: "bold" },
    { token: "patternDefinition", foreground: tokenColor("warm-gold"), fontStyle: "bold" },
    ...callColorRules("dark")
  ],
  colors: {
    ...scrollbarThemeColors,
    "editor.background": themeColor("night-build"),
    "editor.foreground": themeColor("blueprint-white"),
    "editorLineNumber.foreground": themeColor("scaffold-gray"),
    "editorLineNumber.activeForeground": themeColor("blueprint-white"),
    "editorCursor.foreground": themeColor("warm-gold"),
    "editor.selectionBackground": "#D9770640",
    "editor.lineHighlightBackground": themeColor("deep-forge"),
    "editorIndentGuide.background1": "#56565F"
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
    fontFamily: themeColor("font-code"), fontSize: 14, lineHeight: 23,
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
