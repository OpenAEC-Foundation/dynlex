import assert from "node:assert/strict";

export async function assertRiverIncrementalHighlighting({
  evaluate,
  starterSource,
  waitFor
}) {
  const immediateEdit = await evaluate(`(() => {
    const source = document.querySelector('[data-river-source]');
    const code = document.querySelector('[data-river-source-code]');
    const comment = code.querySelector('.river-token-comment');
    const rowToken = [...code.querySelectorAll('.river-token-function')]
      .find((token) => token.textContent === 'row to the other side');
    if (!comment || !rowToken) throw new Error('Starter semantic tokens are missing');
    window.__riverUnaffectedHighlightTokens = [comment, rowToken];
    const lineStart = source.value.indexOf('\\n') + 1;
    const lineEnd = source.value.indexOf('\\n', lineStart);
    source.focus();
    source.setRangeText('get the sheep in the boat', lineStart, lineEnd, 'end');
    source.dispatchEvent(new Event('input', { bubbles: true }));
    return {
      sourceMatches: code.textContent === source.value,
      unaffectedConnected: window.__riverUnaffectedHighlightTokens
        .every((token) => token.isConnected),
      remainingTokens: code.querySelectorAll('.river-token').length
    };
  })()`);
  assert.equal(immediateEdit.sourceMatches, true);
  assert.equal(
    immediateEdit.unaffectedConnected,
    true,
    "Editing one line must preserve the semantic-token nodes on every other line"
  );
  assert.ok(
    immediateEdit.remainingTokens >= 2,
    "Editing one line must not blank semantic highlighting from the rest of the source"
  );

  await waitFor(
    "document.querySelector('[data-river-editor-shell]').dataset.highlightState === 'semantic'"
      + " && [...document.querySelectorAll('[data-river-source-code] .river-token-function')]"
      + ".some((token) => token.textContent === 'sheep')",
    "the edited line to receive live lexical semantic tokens"
  );
  await evaluate(`(() => {
    const code = document.querySelector('[data-river-source-code]');
    window.__riverPreCommitHighlightTokens = [...code.querySelectorAll('.river-token')];
    const source = document.querySelector('[data-river-source]');
    source.setSelectionRange(0, 0);
    source.dispatchEvent(new Event('selectionchange', { bubbles: true }));
  })()`);
  await waitFor(
    "window.__riverPreCommitHighlightTokens.every((token) => !token.isConnected)"
      + " && document.querySelector('[data-river-editor-shell]').dataset.highlightState === 'semantic'",
    "leaving the edited line to replace the complete semantic-token document"
  );
  assert.equal(
    await evaluate(
      "[...document.querySelectorAll('[data-river-source-code] .river-token-function')]"
        + ".some((token) => token.textContent === ' in the boat')"
    ),
    true,
    "Committing the line must replace lexical word tokens with compiled expression tokens"
  );

  await evaluate(`(() => {
    const source = document.querySelector('[data-river-source]');
    const lineStart = source.value.indexOf('\\n') + 1;
    const lineEnd = source.value.indexOf('\\n', lineStart);
    source.setRangeText('get the hay in the boat', lineStart, lineEnd, 'end');
    source.dispatchEvent(new Event('input', { bubbles: true }));
    source.blur();
  })()`);
  await waitFor(
    `document.querySelector('[data-river-source]').value === ${JSON.stringify(starterSource)}`
      + " && document.querySelector('[data-river-source-code]').textContent"
      + " === document.querySelector('[data-river-source]').value"
      + " && document.querySelector('[data-river-editor-shell]').dataset.highlightState === 'semantic'",
    "blurring the editor to commit and fully highlight the restored starter source"
  );

  const immediateLineBreak = await evaluate(
    "(() => {"
      + " const source = document.querySelector('[data-river-source]');"
      + " const code = document.querySelector('[data-river-source-code]');"
      + " window.__riverTokensBeforeLineBreak = [...code.querySelectorAll('.river-token')];"
      + " if (window.__riverTokensBeforeLineBreak.length < 3)"
      + " throw new Error('Restored semantic tokens are missing');"
      + " const firstLineEnd = source.value.indexOf('\\n');"
      + " const secondLineEnd = source.value.indexOf('\\n', firstLineEnd + 1);"
      + " source.focus();"
      + " source.setRangeText('\\n', secondLineEnd, secondLineEnd, 'end');"
      + " source.dispatchEvent(new Event('input', { bubbles: true }));"
      + " return {"
      + " sourceMatches: code.textContent === source.value,"
      + " unaffectedConnected: window.__riverTokensBeforeLineBreak"
      + ".every((token) => token.isConnected),"
      + " remainingTokens: code.querySelectorAll('.river-token').length"
      + " };"
      + "})()"
  );
  assert.equal(immediateLineBreak.sourceMatches, true);
  assert.equal(
    immediateLineBreak.unaffectedConnected,
    true,
    "Pressing Enter must retain every existing semantic-token node"
  );
  assert.ok(
    immediateLineBreak.remainingTokens >= 2,
    "Pressing Enter must retain the existing highlighting until full tokens arrive"
  );
  await waitFor(
    "window.__riverTokensBeforeLineBreak.every((token) => !token.isConnected)"
      + " && document.querySelector('[data-river-editor-shell]').dataset.highlightState === 'semantic'",
    "the committed line break to receive a complete semantic-token document"
  );

  await evaluate(
    "(() => {"
      + " const source = document.querySelector('[data-river-source]');"
      + " const firstLineEnd = source.value.indexOf('\\n');"
      + " const insertedLineBreak = source.value.indexOf('\\n', firstLineEnd + 1);"
      + " source.setRangeText('', insertedLineBreak, insertedLineBreak + 1, 'end');"
      + " source.dispatchEvent(new Event('input', { bubbles: true }));"
      + " source.blur();"
      + "})()"
  );
  await waitFor(
    "document.querySelector('[data-river-source]').value === " + JSON.stringify(starterSource)
      + " && document.querySelector('[data-river-source-code]').textContent"
      + " === document.querySelector('[data-river-source]').value"
      + " && document.querySelector('[data-river-editor-shell]').dataset.highlightState === 'semantic'",
    "removing the inserted line break to restore the starter source"
  );
}
