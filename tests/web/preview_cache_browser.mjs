import assert from "node:assert/strict";
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { command, evaluate, waitFor, closeBrowserSession } from "./browser_test_driver.mjs";

const root = fileURLToPath(new URL("../../", import.meta.url));
const directory = await mkdtemp(path.join(tmpdir(), "dynlex-preview-cache-"));
await writeFile(path.join(directory, "index.html"), "<html><body>Preview cache test</body></html>");
const moduleSource = version => `export const version = ${version};`;
await writeFile(path.join(directory, "role.js"), moduleSource(1));

// Match the former preview server: Last-Modified, with no explicit cache policy.
const oldServer = createServer(async (request, response) => {
  const html = request.url === "/";
  response.setHeader("Content-Type", html ? "text/html" : "text/javascript");
  response.setHeader("Last-Modified", new Date(Date.now() - 7 * 86400000).toUTCString());
  response.end(await readFile(path.join(directory, html ? "index.html" : "role.js")));
});
await new Promise(resolve => oldServer.listen(0, "127.0.0.1", resolve));
const port = oldServer.address().port, origin = `http://127.0.0.1:${port}`;
let preview = null;
try {
  await command("Page.navigate", {url: origin});
  await waitFor("document.body?.textContent==='Preview cache test'", "cache fixture");
  await evaluate("localStorage.setItem('dynlex.games.guest', 'saved farm'); document.cookie='previewSession=retained; SameSite=Lax'");
  assert.equal(await evaluate("import('/role.js').then(module => module.version)"), 1);
  await writeFile(path.join(directory, "role.js"), moduleSource(2));
  await command("Page.reload", {ignoreCache: true});
  await waitFor("document.readyState==='complete'", "hard reload");
  assert.equal(await evaluate("import('/role.js').then(module => module.version)"), 1,
    "A delayed import reproduces the stale module even after hard reload");
  assert.equal(await evaluate("fetch('/role.js', {cache:'no-store'}).then(response => response.text())"), moduleSource(2));

  await new Promise(resolve => oldServer.close(resolve));
  preview = spawn("python3", [path.join(root, "scripts/serve_web.py"), String(port), "--directory", directory], {stdio: "pipe"});
  let log = "";
  preview.stderr.on("data", chunk => { log += chunk; });
  preview.stdout.on("data", chunk => { log += chunk; });
  for (let attempt = 0; ; attempt++) {
    if (preview.exitCode !== null || attempt === 100) throw new Error(`Preview did not start: ${log}`);
    try { if ((await fetch(origin)).ok) break; } catch {}
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  await command("Page.reload", {ignoreCache: true});
  await waitFor("document.readyState==='complete'", "updated preview reload");
  assert.equal(await evaluate("import('/role.js').then(module => module.version)"), 2,
    "The preview must evict assets cached by the former server");
  assert.equal(await evaluate("localStorage.getItem('dynlex.games.guest')"), "saved farm");
  assert.equal(await evaluate("document.cookie"), "previewSession=retained");

  await writeFile(path.join(directory, "role.js"), moduleSource(3));
  await command("Page.reload");
  await waitFor("document.readyState==='complete'", "ordinary preview reload");
  assert.equal(await evaluate("import('/role.js').then(module => module.version)"), 3,
    "An ordinary reload must load the next edit too");
  const document = await fetch(origin), script = await fetch(origin + "/role.js");
  assert.equal(document.headers.get("cache-control"), "no-store");
  assert.equal(document.headers.get("clear-site-data"), '"cache"');
  assert.equal(script.headers.get("cache-control"), "no-store");
  assert.equal(script.headers.get("clear-site-data"), null);
  console.log("Preview reloads replace stale delayed imports, retain progress and cookies, and load subsequent edits.");
} finally {
  if (oldServer.listening) await new Promise(resolve => oldServer.close(resolve));
  if (preview && preview.exitCode === null) { preview.kill(); await once(preview, "exit"); }
  await rm(directory, {recursive: true});
  closeBrowserSession();
}
