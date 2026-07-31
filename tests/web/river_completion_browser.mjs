import assert from "node:assert/strict";

export async function assertRiverEnterCommitsLine({ dispatchKey, evaluate, waitFor }) {
  await evaluate(`(() => {
    const source = document.querySelector('[data-river-source]');
    const lineStart = source.value.lastIndexOf('\\n') + 1;
    source.setRangeText('ro', lineStart, source.value.length, 'end');
    source.dispatchEvent(new Event('input', { bubbles: true }));
  })()`);
  await waitFor(
    "[...document.querySelectorAll('[data-river-completion]')]"
      + ".some((item) => item.querySelector('strong').textContent === 'row ')",
    "real DynLex row completion before committing the line with Enter"
  );
  await dispatchKey("Enter", "Enter", 13, 0, "\r");
  assert.match(
    await evaluate("document.querySelector('[data-river-source]').value"),
    /\nro\n$/,
    "Enter must finish the active line without accepting its selected completion"
  );
}
