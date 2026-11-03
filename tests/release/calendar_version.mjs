import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import {
  matchingProjectVersion,
  nextCalendarVersion,
  parseCalendarVersion,
  readProjectVersions,
  updateProjectVersion,
} from "../../scripts/update-version.mjs";

assert.deepEqual(parseCalendarVersion("2026.45.0"), { year: 2026, week: 45, revision: 0 });
for (const invalid of ["0.0.3", "2026.0.0", "2026.54.0", "2025.53.0", "2026.045.0", "2026.45.65536"]) {
  assert.throws(() => parseCalendarVersion(invalid), /invalid calendar version/i, invalid);
}
assert.equal(nextCalendarVersion("2026.44.9", new Date("2026-11-02T12:00:00Z")), "2026.45.0");
assert.equal(nextCalendarVersion("2026.45.0", new Date("2026-11-02T12:00:00Z")), "2026.45.1");
assert.equal(nextCalendarVersion("2026.52.2", new Date("2027-01-01T12:00:00Z")), "2026.53.0");
assert.equal(nextCalendarVersion("2026.53.3", new Date("2027-01-04T12:00:00Z")), "2027.1.0");
assert.throws(() => nextCalendarVersion("2026.46.0", new Date("2026-11-02T12:00:00Z")), /future/i);
const moduleUrl = new URL("../../scripts/update-version.mjs", import.meta.url).href;
const imported = spawnSync(process.execPath, ["--input-type=module", "-"], {
  input: `import { parseCalendarVersion } from ${JSON.stringify(moduleUrl)}; console.log(parseCalendarVersion("2026.45.1").week);`,
  encoding: "utf8",
});
assert.equal(imported.status, 0, imported.stderr);
assert.equal(imported.stdout.trim(), "45");

const root = fs.mkdtempSync(path.join(os.tmpdir(), "dynlex-calendar-version-"));
try {
  fs.mkdirSync(path.join(root, "metadata"));
  fs.mkdirSync(path.join(root, "vscode-extension"));
  fs.mkdirSync(path.join(root, "tools/wgsl-translator"), { recursive: true });
  fs.writeFileSync(path.join(root, "metadata/VERSION"), "0.0.3\n");
  fs.writeFileSync(path.join(root, "vscode-extension/package.json"), JSON.stringify({ version: "0.0.3" }));
  fs.writeFileSync(path.join(root, "vscode-extension/package-lock.json"), JSON.stringify({
    version: "0.0.3", packages: { "": { version: "0.0.3" } },
  }));
  fs.writeFileSync(path.join(root, "tools/wgsl-translator/Cargo.toml"),
    '[package]\nname = "dynlex-wgsl-translator"\nversion = "0.1.0"\n\n[dependencies]\nnaga = "30.0.1"\n');
  fs.writeFileSync(path.join(root, "tools/wgsl-translator/Cargo.lock"),
    'version = 4\n\n[[package]]\nname = "dynlex-wgsl-translator"\nversion = "0.1.0"\n\n[[package]]\nname = "naga"\nversion = "30.0.1"\n');
  updateProjectVersion(root, "2026.45.0");
  assert.deepEqual(new Set(Object.values(readProjectVersions(root))), new Set(["2026.45.0"]));
  assert.equal(matchingProjectVersion(root), "2026.45.0");
  const manifest = fs.readFileSync(path.join(root, "tools/wgsl-translator/Cargo.toml"), "utf8");
  const lock = fs.readFileSync(path.join(root, "tools/wgsl-translator/Cargo.lock"), "utf8");
  assert.match(manifest, /version = "2026\.45\.0"/);
  assert.match(manifest, /naga = "30\.0\.1"/);
  assert.match(lock, /name = "naga"\nversion = "30\.0\.1"/);
  for (const filename of ["Cargo.toml", "Cargo.lock"]) {
    const file = path.join(root, "tools/wgsl-translator", filename);
    fs.writeFileSync(file, fs.readFileSync(file, "utf8").replace(/\n/g, "\r\n"));
  }
  updateProjectVersion(root, "2026.45.1");
  assert.equal(matchingProjectVersion(root), "2026.45.1");
  for (const filename of ["Cargo.toml", "Cargo.lock"]) {
    const contents = fs.readFileSync(path.join(root, "tools/wgsl-translator", filename), "utf8");
    assert.match(contents, /version = "2026\.45\.1"\r\n/);
    assert.doesNotMatch(contents, /(?<!\r)\n/);
  }
  assert.match(fs.readFileSync(path.join(root, "tools/wgsl-translator/Cargo.lock"), "utf8"),
    /name = "naga"\r\nversion = "30\.0\.1"/);
  fs.mkdirSync(path.join(root, "scripts"));
  fs.copyFileSync(new URL("../../scripts/update-version.mjs", import.meta.url), path.join(root, "scripts/update-version.mjs"));
  const alias = path.join(root, "alias");
  fs.symlinkSync(root, alias, process.platform === "win32" ? "junction" : "dir");
  const command = spawnSync(process.execPath, [path.join(alias, "scripts/update-version.mjs"), "--check"], { encoding: "utf8" });
  assert.equal(command.status, 0, command.stderr);
  assert.equal(command.stdout.trim(), "2026.45.1");
  fs.writeFileSync(path.join(root, "vscode-extension/package.json"), JSON.stringify({ version: "2026.45.2" }));
  assert.throws(() => matchingProjectVersion(root), /do not match/i);
} finally {
  fs.rmSync(root, { recursive: true });
}
console.log("calendar versions: passed");
