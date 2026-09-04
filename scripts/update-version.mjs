#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const versionPattern = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;

function projectVersionFiles(projectDirectory) {
  return {
    native: path.join(projectDirectory, "metadata/VERSION"),
    extensionManifest: path.join(projectDirectory, "vscode-extension/package.json"),
    extensionLock: path.join(projectDirectory, "vscode-extension/package-lock.json"),
  };
}

function readVersionState(projectDirectory) {
  const files = projectVersionFiles(projectDirectory);
  const native = fs.readFileSync(files.native, "utf8").trim();
  const extensionManifest = JSON.parse(fs.readFileSync(files.extensionManifest, "utf8"));
  const extensionLock = JSON.parse(fs.readFileSync(files.extensionLock, "utf8"));
  const extensionLockRoot = extensionLock.packages?.[""];
  if (!extensionLockRoot || typeof extensionLockRoot !== "object") {
    throw new Error("extension package lock has no root package metadata");
  }
  const versions = {
    native,
    extensionManifest: extensionManifest.version,
    extensionLock: extensionLock.version,
    extensionLockRoot: extensionLockRoot.version,
  };
  for (const [source, version] of Object.entries(versions)) {
    if (typeof version !== "string" || !versionPattern.test(version)) {
      throw new Error(`${source} has an invalid version: ${String(version)}`);
    }
  }
  return { files, extensionManifest, extensionLock, extensionLockRoot, versions };
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
  return versions.native;
}

export function updateProjectVersion(projectDirectory, version) {
  if (!versionPattern.test(version)) {
    throw new Error(`invalid version: ${version}`);
  }
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
}

const invokedAsScript = process.argv[1]
  && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url));
if (invokedAsScript) {
  if (process.argv.length !== 3) {
    console.error(`Usage: ${path.basename(process.argv[1])} <X.Y.Z> | --check`);
    process.exit(2);
  }
  const projectDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
  try {
    if (process.argv[2] === "--check") {
      console.log(matchingProjectVersion(projectDirectory));
    } else {
      updateProjectVersion(projectDirectory, process.argv[2]);
    }
  } catch (error) {
    console.error(`Error: ${error.message}`);
    process.exit(1);
  }
}
