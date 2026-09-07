import assert from "node:assert/strict";
import { mkdtemp, readFile, readdir, rm, stat } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(testDirectory, "../..");
const temporaryDirectory = await mkdtemp(path.join(os.tmpdir(), "dynlex-web-deployment-"));
const release = "0123456789abcdef0123456789abcdef01234567";
const outputDirectory = path.join(temporaryDirectory, "site");

async function collectHtmlFiles(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const entryPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      files.push(...await collectHtmlFiles(entryPath));
    } else if (entry.isFile() && entry.name.endsWith(".html")) {
      files.push(entryPath);
    }
  }
  return files;
}

try {
  const deploymentWorkflow = await readFile(
    path.join(projectDirectory, ".github/workflows/deploy.yml"),
    "utf8"
  );
  assert.match(deploymentWorkflow, /RELEASE: \$\{\{ github\.sha \}\}/);
  assert.match(deploymentWorkflow, /DYNLEX_WEB_BASE="\/releases\/\$RELEASE\/"/);
  assert.match(deploymentWorkflow, /mv -Tf "\$DEPLOY_PATH\/current\.next" "\$DEPLOY_PATH\/current"/);
  assert.match(deploymentWorkflow, /max-age=31536000, immutable/);
  assert.match(deploymentWorkflow, /add_header Cache-Control "no-cache" always/);
  assert.match(deploymentWorkflow, /-mmin \+1440/);

  const result = spawnSync(
    process.execPath,
    [path.join(projectDirectory, "scripts/prepare_web_deployment.mjs"), release, outputDirectory],
    { encoding: "utf8" }
  );
  assert.equal(result.status, 0, result.stderr);

  const homepage = await readFile(path.join(outputDirectory, "index.html"), "utf8");
  assert.match(homepage, new RegExp(`src="/releases/${release}/homepage\\.js"`));
  assert.match(homepage, new RegExp(`href="/releases/${release}/style\\.css"`));
  assert.match(homepage, new RegExp(`src="/releases/${release}/media/river-challenge/boat\\.webp"`));
  assert.match(homepage, /href="ide\/index\.html"/);

  const documentation = await readFile(path.join(outputDirectory, "wiki/sections/loop.html"), "utf8");
  assert.match(documentation, new RegExp(`href="/releases/${release}/style\\.css"`));
  assert.match(documentation, /href="\.\.\/terms\.html#condition"/);

  for (const htmlPath of await collectHtmlFiles(outputDirectory)) {
    const html = await readFile(htmlPath, "utf8");
    for (const tag of html.matchAll(/<(?:img|link|script|source|video|audio)\b[^>]*>/gi)) {
      for (const attribute of tag[0].matchAll(/\s(?:src|href|poster)\s*=\s*["'](.*?)["']/gi)) {
        const url = attribute[1];
        if (/^(?:[a-z][a-z0-9+.-]*:|\/\/|#)/i.test(url)) {
          continue;
        }
        assert.ok(url.startsWith(`/releases/${release}/`), `${htmlPath} contains unversioned resource ${url}`);
        const assetPath = path.join(outputDirectory, url.slice(`/releases/${release}/`.length));
        assert.equal((await stat(assetPath)).isFile(), true, `${url} does not identify a release file`);
      }
    }
  }
} finally {
  await rm(temporaryDirectory, { recursive: true, force: true });
}
