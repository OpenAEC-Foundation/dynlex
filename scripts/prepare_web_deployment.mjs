#!/usr/bin/env node

import { cp, mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(scriptDirectory, "..");
const sourceDirectory = path.join(projectDirectory, "web");
const [release, outputArgument] = process.argv.slice(2);

if (!release || !/^[0-9a-f]{40}$/.test(release)) {
  throw new Error("Usage: prepare_web_deployment.mjs <40-character commit SHA> <output directory>");
}
if (!outputArgument) {
  throw new Error("Deployment output directory is required");
}

const outputDirectory = path.resolve(outputArgument);
const releaseBase = `/releases/${release}/`;
const resourceAttributes = Object.freeze({
  img: Object.freeze(["src", "srcset"]),
  link: Object.freeze(["href"]),
  script: Object.freeze(["src"]),
  source: Object.freeze(["src", "srcset"]),
  video: Object.freeze(["poster", "src"]),
  audio: Object.freeze(["src"])
});

function versionUrl(value, documentPath) {
  if (/^(?:[a-z][a-z0-9+.-]*:|\/\/|#)/i.test(value)) {
    return value;
  }
  if (value.startsWith(releaseBase)) {
    return value;
  }
  const absolutePath = value.startsWith("/")
    ? value.slice(1)
    : path.posix.normalize(path.posix.join(path.posix.dirname(documentPath), value));
  if (absolutePath.startsWith("../") || absolutePath === "..") {
    throw new Error(`Resource escapes the web root: ${value} in ${documentPath}`);
  }
  return `${releaseBase}${absolutePath}`;
}

function versionSrcset(value, documentPath) {
  return value.split(",").map((candidate) => {
    const match = candidate.trim().match(/^(\S+)(\s+.+)?$/);
    if (!match) {
      throw new Error(`Invalid srcset in ${documentPath}: ${value}`);
    }
    return `${versionUrl(match[1], documentPath)}${match[2] ?? ""}`;
  }).join(", ");
}

function versionHtml(source, documentPath) {
  return source.replace(/<(img|link|script|source|video|audio)\b[^>]*>/gi, (tag, rawName) => {
    const name = rawName.toLowerCase();
    let rewritten = tag;
    for (const attribute of resourceAttributes[name]) {
      const expression = new RegExp(`(\\s${attribute}\\s*=\\s*)(["'])(.*?)\\2`, "i");
      rewritten = rewritten.replace(expression, (match, prefix, quote, value) => {
        const versioned = attribute === "srcset"
          ? versionSrcset(value, documentPath)
          : versionUrl(value, documentPath);
        return `${prefix}${quote}${versioned}${quote}`;
      });
    }
    return rewritten;
  });
}

async function htmlFiles(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const entryPath = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      files.push(...await htmlFiles(entryPath));
    } else if (entry.isFile() && entry.name.endsWith(".html")) {
      files.push(entryPath);
    }
  }
  return files;
}

await mkdir(path.dirname(outputDirectory), { recursive: true });
await cp(sourceDirectory, outputDirectory, { recursive: true, force: false, errorOnExist: true });
for (const htmlPath of await htmlFiles(outputDirectory)) {
  const documentPath = path.relative(outputDirectory, htmlPath).split(path.sep).join("/");
  const source = await readFile(htmlPath, "utf8");
  await writeFile(htmlPath, versionHtml(source, documentPath));
}
