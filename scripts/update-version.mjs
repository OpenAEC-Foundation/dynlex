#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const versionPattern = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const translatorPackageName = "dynlex-wgsl-translator";

function isoWeek(date) {
  const thursday = new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate()));
  thursday.setUTCDate(thursday.getUTCDate() + 4 - (thursday.getUTCDay() || 7));
  const year = thursday.getUTCFullYear();
  const week = Math.ceil(((thursday - Date.UTC(year, 0, 1)) / 86400000 + 1) / 7);
  return { year, week };
}

export function parseCalendarVersion(version) {
  if (typeof version !== "string" || !versionPattern.test(version)) {
    throw new Error(`invalid calendar version: ${String(version)}`);
  }
  const [year, week, revision] = version.split(".").map(Number);
  const lastWeek = isoWeek(new Date(Date.UTC(year, 11, 28))).week;
  // MSI stores the year as an offset from 2000 in an 8-bit field, and the
  // within-week revision in a 16-bit field.
  if (year < 2000 || year > 2255 || week < 1 || week > lastWeek || revision > 65535) {
    throw new Error(`invalid calendar version: ${version}`);
  }
  return { year, week, revision };
}

export function nextCalendarVersion(currentVersion, date = new Date()) {
  const current = parseCalendarVersion(currentVersion);
  const { year, week } = isoWeek(date);
  if (current.year > year || (current.year === year && current.week > week)) {
    throw new Error(`current version ${currentVersion} belongs to a future ISO week`);
  }
  const revision = current.year === year && current.week === week ? current.revision + 1 : 0;
  const next = `${year}.${week}.${revision}`;
  parseCalendarVersion(next);
  return next;
}

function rustPackage(contents, header) {
  const sections = contents.split(/(?=^\[)/m);
  const indices = sections.flatMap((section, index) => {
    const lines = section.split(/\r?\n/);
    return lines[0] === header && lines.includes(`name = "${translatorPackageName}"`) ? [index] : [];
  });
  if (indices.length !== 1) {
    throw new Error(`expected one ${translatorPackageName} package in ${header} metadata`);
  }
  const index = indices[0];
  const match = /^version = "([^"]+)"\r?$/m.exec(sections[index]);
  if (!match) {
    throw new Error(`${translatorPackageName} has no package version`);
  }
  return {
    version: match[1],
    withVersion(version) {
      sections[index] = sections[index].replace(/^version = "[^"]+"(\r?)$/m,
        (_line, carriageReturn) => `version = "${version}"${carriageReturn}`);
      return sections.join("");
    },
  };
}

function projectVersionFiles(projectDirectory) {
  return {
    native: path.join(projectDirectory, "metadata/VERSION"),
    extensionManifest: path.join(projectDirectory, "vscode-extension/package.json"),
    extensionLock: path.join(projectDirectory, "vscode-extension/package-lock.json"),
    translatorManifest: path.join(projectDirectory, "tools/wgsl-translator/Cargo.toml"),
    translatorLock: path.join(projectDirectory, "tools/wgsl-translator/Cargo.lock"),
  };
}

function readVersionState(projectDirectory) {
  const files = projectVersionFiles(projectDirectory);
  const native = fs.readFileSync(files.native, "utf8").trim();
  const extensionManifest = JSON.parse(fs.readFileSync(files.extensionManifest, "utf8"));
  const extensionLock = JSON.parse(fs.readFileSync(files.extensionLock, "utf8"));
  const translatorManifest = rustPackage(fs.readFileSync(files.translatorManifest, "utf8"), "[package]");
  const translatorLock = rustPackage(fs.readFileSync(files.translatorLock, "utf8"), "[[package]]");
  const extensionLockRoot = extensionLock.packages?.[""];
  if (!extensionLockRoot || typeof extensionLockRoot !== "object") {
    throw new Error("extension package lock has no root package metadata");
  }
  const versions = {
    native,
    extensionManifest: extensionManifest.version,
    extensionLock: extensionLock.version,
    extensionLockRoot: extensionLockRoot.version,
    translatorManifest: translatorManifest.version,
    translatorLock: translatorLock.version,
  };
  for (const [source, version] of Object.entries(versions)) {
    if (typeof version !== "string" || !versionPattern.test(version)) {
      throw new Error(`${source} has an invalid version: ${String(version)}`);
    }
  }
  return { files, extensionManifest, extensionLock, extensionLockRoot, translatorManifest, translatorLock, versions };
}

export function readProjectVersions(projectDirectory) {
  return readVersionState(projectDirectory).versions;
}

export function matchingProjectVersion(projectDirectory) {
  const versions = readProjectVersions(projectDirectory);
  if (new Set(Object.values(versions)).size !== 1) {
    const details = Object.entries(versions)
      .map(([source, version]) => `${source}=${version}`)
      .join(", ");
    throw new Error(`project versions do not match: ${details}`);
  }
  parseCalendarVersion(versions.native);
  return versions.native;
}

export function updateProjectVersion(projectDirectory, version) {
  parseCalendarVersion(version);
  const state = readVersionState(projectDirectory);
  state.extensionManifest.version = version;
  state.extensionLock.version = version;
  state.extensionLockRoot.version = version;
  fs.writeFileSync(state.files.native, `${version}\n`);
  fs.writeFileSync(
    state.files.extensionManifest,
    `${JSON.stringify(state.extensionManifest, null, 2)}\n`,
  );
  fs.writeFileSync(
    state.files.extensionLock,
    `${JSON.stringify(state.extensionLock, null, 2)}\n`,
  );
  fs.writeFileSync(state.files.translatorManifest, state.translatorManifest.withVersion(version));
  fs.writeFileSync(state.files.translatorLock, state.translatorLock.withVersion(version));
}

const invokedAsScript = process.argv[1]
  && fs.existsSync(process.argv[1])
  && fs.realpathSync(process.argv[1]) === fs.realpathSync(fileURLToPath(import.meta.url));
if (invokedAsScript) {
  const arguments_ = process.argv.slice(2);
  if (arguments_.length !== 1 && !(arguments_.length === 2 && arguments_[0] === "--validate")) {
    console.error(`Usage: ${path.basename(process.argv[1])} <YEAR.WEEK.REVISION> | --check | --next | --validate <version>`);
    process.exit(2);
  }
  const projectDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
  try {
    if (arguments_[0] === "--validate") {
      parseCalendarVersion(arguments_[1]);
      console.log(arguments_[1]);
    } else if (arguments_[0] === "--check") {
      console.log(matchingProjectVersion(projectDirectory));
    } else if (arguments_[0] === "--next") {
      console.log(nextCalendarVersion(matchingProjectVersion(projectDirectory)));
    } else {
      updateProjectVersion(projectDirectory, arguments_[0]);
    }
  } catch (error) {
    console.error(`Error: ${error.message}`);
    process.exit(1);
  }
}
