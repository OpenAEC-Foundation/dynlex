import assert from "node:assert/strict";
import { evaluate, navigate, waitFor } from "./browser_test_driver.mjs";

export async function checkSanitizer() {
  await navigate("/tests/sanitizer.html");
  await waitFor("Boolean(globalThis.sanitizer)", "the sanitizer fixture to initialize");
  const result = await evaluate(`(async () => {
    const { purify, sanitizeHtml } = globalThis.sanitizer;
    const shadowResults = [];
    for (const nested of [false, true]) {
      for (const expression of ['$' + '{7*7}', '{{7*7}}', '<% 7*7 %>']) {
        const host = document.createElement('div');
        let shadow = host.attachShadow({mode: 'open'});
        if (nested) {
          const child = document.createElement('div');
          shadow.append(child);
          shadow = child.attachShadow({mode: 'open'});
        }
        for (const character of expression) {
          shadow.append(document.createTextNode(character), document.createElement('object'));
        }
        purify.sanitize(host, {IN_PLACE: true, SAFE_FOR_TEMPLATES: true});
        shadow.normalize();
        shadowResults.push(shadow.textContent);
      }
    }
    const safeHost = document.createElement('div');
    const safeShadow = safeHost.attachShadow({mode: 'open'});
    safeShadow.textContent = 'Plain shadow content';
    purify.sanitize(safeHost, {IN_PLACE: true, SAFE_FOR_TEMPLATES: true});
    const container = document.createElement('div');
    container.innerHTML = sanitizeHtml(
      '<p>Hello <strong>world</strong> <a href="https://example.com/help">help</a></p>' +
      '<img src="x" onerror="window.sanitizerFailure=true">' +
      '<a href="javascript:window.sanitizerFailure=true">bad link</a>' +
      '<script>window.sanitizerFailure=true</script>'
    );
    return {
      shadowResults,
      plainShadow: safeShadow.textContent,
      legitimateText: container.querySelector('strong')?.textContent,
      legitimateLink: container.querySelector('a')?.getAttribute('href'),
      unsafeAttributes: [...container.querySelectorAll('*')].flatMap(node =>
        [...node.attributes].filter(attribute =>
          attribute.name.startsWith('on') || attribute.value.startsWith('javascript:')
        ).map(attribute => attribute.name)
      ),
      scripts: container.querySelectorAll('script').length
    };
  })()`);
  for (const text of result.shadowResults) {
    assert.doesNotMatch(text, /\$\{|\{\{|<%/, "Split template expressions survived in a shadow root");
  }
  assert.equal(result.plainShadow, "Plain shadow content");
  assert.equal(result.legitimateText, "world");
  assert.equal(result.legitimateLink, "https://example.com/help");
  assert.deepEqual(result.unsafeAttributes, []);
  assert.equal(result.scripts, 0);
  console.log("Monaco sanitization blocks shadow-root template bypasses and retains ordinary markup.");
}
