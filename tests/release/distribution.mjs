import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import {
  detectPlatform,
  parseReleaseManifest,
  selectReleaseAssets,
} from "../../web/download.js";

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(testDirectory, "../..");
const manifestPath = path.join(projectDirectory, "web/release-manifest.txt");
const manifestText = fs.readFileSync(manifestPath, "utf8");
const manifest = parseReleaseManifest(manifestText);
const releaseWorkflow = fs.readFileSync(
  path.join(projectDirectory, ".github/workflows/release.yml"),
  "utf8",
);
const cmakeConfiguration = fs.readFileSync(
  path.join(projectDirectory, "CMakeLists.txt"),
  "utf8",
);

assert.equal(manifest.schema, 1);
assert.equal(manifest.repository, "OpenAEC-Foundation/dynlex");
assert.deepEqual(
  manifest.assets.map(({ id, os: platform, architectures, format, name }) => ({
    id,
    platform,
    architectures,
    format,
    name,
  })),
  [
    {
      id: "linux-x64-deb",
      platform: "linux",
      architectures: ["x64"],
      format: "deb",
      name: "dynlex-linux-x64.deb",
    },
    {
      id: "linux-x64-tar",
      platform: "linux",
      architectures: ["x64"],
      format: "tar.gz",
      name: "dynlex-linux-x64.tar.gz",
    },
    {
      id: "windows-x64-msi",
      platform: "windows",
      architectures: ["x64"],
      format: "msi",
      name: "dynlex-windows-x64.msi",
    },
    {
      id: "macos-universal-pkg",
      platform: "macos",
      architectures: ["arm64", "x64"],
      format: "pkg",
      name: "dynlex-macos-universal.pkg",
    },
  ],
);

const releaseAssets = manifest.assets.map(({ name }) => ({
  name,
  browser_download_url: `https://downloads.example/${name}`,
}));
releaseAssets.unshift({
  name: "dynlex-0.0.1-Windows.msi",
  browser_download_url: "https://downloads.example/obsolete.msi",
});

const selectedAssets = selectReleaseAssets(releaseAssets, manifest);
for (const asset of manifest.assets) {
  assert.equal(selectedAssets.get(asset.id).name, asset.name);
}
assert.equal(
  selectedAssets.get("windows-x64-msi").browser_download_url,
  "https://downloads.example/dynlex-windows-x64.msi",
);

assert.throws(
  () => selectReleaseAssets(releaseAssets.slice(0, -1), manifest),
  /missing release asset/,
);
assert.throws(
  () => selectReleaseAssets([...releaseAssets, releaseAssets[1]], manifest),
  /duplicate release asset/,
);
assert.throws(
  () => parseReleaseManifest(`${manifestText}asset duplicate linux x64 deb dynlex-linux-x64.deb\n`),
  /duplicate release asset name/,
);

assert.deepEqual(detectPlatform("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"), {
  os: "windows",
  architecture: "x64",
  label: "Windows",
  primaryAssetId: "windows-x64-msi",
});
assert.deepEqual(detectPlatform("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"), {
  os: "macos",
  architecture: "universal",
  label: "macOS",
  primaryAssetId: "macos-universal-pkg",
});
assert.deepEqual(detectPlatform("Mozilla/5.0 (X11; Linux x86_64)"), {
  os: "linux",
  architecture: "x64",
  label: "Linux",
  primaryAssetId: "linux-x64-deb",
});
assert.throws(
  () => detectPlatform("Mozilla/5.0 (X11; Linux aarch64)"),
  /unsupported Linux architecture/,
);
assert.match(releaseWorkflow, /runner: macos-14[\s\S]*architecture: arm64/);
assert.match(releaseWorkflow, /runner: macos-15-intel[\s\S]*architecture: x64/);
assert.match(releaseWorkflow, /lipo -create/);
assert.match(releaseWorkflow, /lipo -verify_arch arm64 x86_64/);
assert.match(releaseWorkflow, /format: \[deb, tar\]/);
assert.match(releaseWorkflow, /prepare-release-assets\.sh/);
assert.match(cmakeConfiguration, /CPACK_PACKAGE_HOMEPAGE_URL "https:\/\/dynlex\.com"/);
assert.doesNotMatch(cmakeConfiguration, /johnheikens\/DynLex/i);

const queryScript = path.join(projectDirectory, "scripts/release-asset.sh");
if (process.platform !== "win32") {
  for (const asset of manifest.assets) {
    const result = spawnSync("bash", [queryScript, "name", asset.id], {
      cwd: projectDirectory,
      encoding: "utf8",
    });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout.trim(), asset.name);
  }
}

const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "dynlex-release-test-"));
try {
  const artifactDirectory = path.join(temporaryDirectory, "artifacts");
  const outputDirectory = path.join(temporaryDirectory, "upload");
  fs.mkdirSync(artifactDirectory);
  for (const asset of manifest.assets) {
    fs.writeFileSync(path.join(artifactDirectory, asset.name), `${asset.id}\n`);
  }

  if (process.platform === "linux") {
    const prepareScript = path.join(projectDirectory, "scripts/prepare-release-assets.sh");
    const prepareResult = spawnSync(
      "bash",
      [prepareScript, artifactDirectory, outputDirectory],
      { cwd: projectDirectory, encoding: "utf8" },
    );
    assert.equal(prepareResult.status, 0, prepareResult.stdout + prepareResult.stderr);

    const outputNames = fs.readdirSync(outputDirectory).sort();
    assert.deepEqual(outputNames, [
      "SHA256SUMS",
      ...manifest.assets.map(({ name }) => name),
      "release-manifest.txt",
    ].sort());

    const checksumResult = spawnSync("sha256sum", ["--check", "SHA256SUMS"], {
      cwd: outputDirectory,
      encoding: "utf8",
    });
    assert.equal(checksumResult.status, 0, checksumResult.stdout + checksumResult.stderr);

    fs.writeFileSync(path.join(artifactDirectory, "unexpected.pkg"), "unexpected\n");
    const unexpectedResult = spawnSync(
      "bash",
      [
        prepareScript,
        artifactDirectory,
        path.join(temporaryDirectory, "unexpected-output"),
      ],
      { cwd: projectDirectory, encoding: "utf8" },
    );
    assert.notEqual(unexpectedResult.status, 0);
    assert.match(unexpectedResult.stderr, /unexpected release artifact/);
  }

  if (process.platform === "linux") {
    const mockBinDirectory = path.join(temporaryDirectory, "mock-bin");
    const packageDirectory = path.join(temporaryDirectory, "packages");
    const tarRootDirectory = path.join(packageDirectory, "dynlex-linux-x64");
    fs.mkdirSync(path.join(tarRootDirectory, "bin"), { recursive: true });
    fs.mkdirSync(mockBinDirectory);

    const writeExecutable = (filePath, contents) => {
      fs.writeFileSync(filePath, contents);
      fs.chmodSync(filePath, 0o755);
    };
    writeExecutable(
      path.join(tarRootDirectory, "bin/dynlex"),
      "#!/bin/sh\n[ \"$1\" = \"--help\" ]\n",
    );
    writeExecutable(path.join(mockBinDirectory, "dpkg"), "#!/bin/sh\nexit 0\n");
    writeExecutable(
      path.join(mockBinDirectory, "id"),
      "#!/bin/sh\n[ \"$1\" = \"-u\" ] && printf '0\\n'\n",
    );
    writeExecutable(path.join(mockBinDirectory, "dynlex"), "#!/bin/sh\nexit 0\n");

    const tarPackage = path.join(packageDirectory, "dynlex-linux-x64.tar.gz");
    const tarResult = spawnSync(
      "tar",
      ["-czf", tarPackage, "-C", packageDirectory, "dynlex-linux-x64"],
      { encoding: "utf8" },
    );
    assert.equal(tarResult.status, 0, tarResult.stderr);

    const installScript = path.join(projectDirectory, "web/install.sh");
    const tarPrefix = path.join(temporaryDirectory, "local-tar-prefix");
    const localTarResult = spawnSync(
      "sh",
      [installScript, "--prefix", tarPrefix, tarPackage],
      { cwd: projectDirectory, encoding: "utf8" },
    );
    assert.equal(localTarResult.status, 0, localTarResult.stdout + localTarResult.stderr);
    assert.ok(fs.existsSync(path.join(tarPrefix, "bin/dynlex")));

    const debPackage = path.join(packageDirectory, "dynlex-linux-x64.deb");
    fs.writeFileSync(debPackage, "mock Debian package\n");
    const mockEnvironment = {
      ...process.env,
      PATH: `${mockBinDirectory}:${process.env.PATH}`,
    };
    const localDebResult = spawnSync("sh", [installScript, debPackage], {
      cwd: projectDirectory,
      encoding: "utf8",
      env: mockEnvironment,
    });
    assert.equal(localDebResult.status, 0, localDebResult.stdout + localDebResult.stderr);

    const checksum = spawnSync("sha256sum", [tarPackage], { encoding: "utf8" });
    assert.equal(checksum.status, 0, checksum.stderr);
    const checksumPath = path.join(temporaryDirectory, "fixture-SHA256SUMS");
    fs.writeFileSync(
      checksumPath,
      `${checksum.stdout.trim().split(/\s+/)[0]}  dynlex-linux-x64.tar.gz\n`,
    );
    writeExecutable(
      path.join(mockBinDirectory, "curl"),
      `#!/bin/sh
case "$2" in
  */release-manifest.txt) cp "$DYNLEX_TEST_MANIFEST" "$4" ;;
  */dynlex-linux-x64.tar.gz) cp "$DYNLEX_TEST_TAR" "$4" ;;
  */SHA256SUMS) cp "$DYNLEX_TEST_CHECKSUMS" "$4" ;;
  *) exit 2 ;;
esac
`,
    );

    const onlinePrefix = path.join(temporaryDirectory, "online-tar-prefix");
    const onlineTarResult = spawnSync(
      "sh",
      [installScript, "--format", "tar", "--prefix", onlinePrefix],
      {
        cwd: projectDirectory,
        encoding: "utf8",
        env: {
          ...mockEnvironment,
          DYNLEX_TEST_MANIFEST: manifestPath,
          DYNLEX_TEST_TAR: tarPackage,
          DYNLEX_TEST_CHECKSUMS: checksumPath,
        },
      },
    );
    assert.equal(onlineTarResult.status, 0, onlineTarResult.stdout + onlineTarResult.stderr);
    assert.ok(fs.existsSync(path.join(onlinePrefix, "bin/dynlex")));
  }
} finally {
  fs.rmSync(temporaryDirectory, { recursive: true, force: true });
}

console.log("Release artifacts, downloads, and checksums share one manifest.");
