import { evaluate } from "./browser_test_driver.mjs";

export function withEditor(selector, script) {
  return evaluate(`(async () => {
    const {monaco} = await import('/editor.js');
    const host = document.querySelector(${JSON.stringify(selector)});
    const editor = monaco.editor.getEditors().find(editor => editor.getContainerDomNode() === host);
    if (!editor) throw new Error('Missing editor: '+${JSON.stringify(selector)});
    const model = editor.getModel();
    ${script}
  })()`);
}
export const readCode = selector => withEditor(selector, 'return model.getValue();');
export const editCode = (selector, value) => withEditor(selector, `editor.executeEdits('test', [{range:model.getFullModelRange(),text:${JSON.stringify(value)}}]);`);

// Test-only access to Monaco's public registry, also installed after reloads.
export async function installEditorAccess() {
  const {command} = await import('./browser_test_driver.mjs');
  const source = `(async () => {
    const {monaco} = await import('/editor.js');
    window.editorFor = selector => monaco.editor.getEditors().find(editor => editor.getContainerDomNode() === document.querySelector(selector));
    window.writeEditorCode = (selector, text) => {
      const editor = editorFor(selector), model = editor.getModel();
      editor.executeEdits('test', [{range:model.getFullModelRange(),text}]);
    };
    window.editorFeedback = (selector, state) => {
      const model = editorFor(selector).getModel();
      const decoration = model.getAllDecorations().find(d => d.options.className === 'dynlex-code-'+state);
      return decoration ? {line:String(decoration.range.startLineNumber),text:model.getValueInRange(decoration.range),state} : null;
    };
    window.editorMarkers = selector => monaco.editor.getModelMarkers({resource:editorFor(selector).getModel().uri});
  })()`;
  await command('Page.addScriptToEvaluateOnNewDocument',{source});
  await evaluate(source);
}
