import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import {
  detectPlatform,
  parseReleaseManifest,
  selectPrimaryReleaseAsset,
  selectReleaseAssets,
} from "../../web/download.js";

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectDirectory = path.resolve(testDirectory, "../..");
const manifestPath = path.join(projectDirectory, "metadata/release-manifest.txt");
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
      id: "linux-arm64-deb",
      platform: "linux",
      architectures: ["arm64"],
      format: "deb",
      name: "dynlex-linux-arm64.deb",
    },
    {
      id: "linux-arm64-tar",
      platform: "linux",
      architectures: ["arm64"],
      format: "tar.gz",
      name: "dynlex-linux-arm64.tar.gz",
    },
    {
      id: "windows-x64-msi",
      platform: "windows",
      architectures: ["x64"],
      format: "msi",
      name: "dynlex-windows-x64.msi",
    },
    {
      id: "windows-arm64-msi",
      platform: "windows",
      architectures: ["arm64"],
      format: "msi",
      name: "dynlex-windows-arm64.msi",
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
});
assert.deepEqual(
  detectPlatform("Mozilla/5.0 (Windows NT 10.0; ARM64)", "arm"),
  {
    os: "windows",
    architecture: "arm64",
    label: "Windows",
  },
);
assert.deepEqual(detectPlatform("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"), {
  os: "macos",
  architecture: "unknown",
  label: "macOS",
});
assert.deepEqual(detectPlatform("Mozilla/5.0 (X11; Linux x86_64)"), {
  os: "linux",
  architecture: "x64",
  label: "Linux",
});
assert.deepEqual(
  detectPlatform("Mozilla/5.0 (X11; Linux aarch64)"),
  {
    os: "linux",
    architecture: "arm64",
    label: "Linux",
  },
);
assert.equal(
  selectPrimaryReleaseAsset(manifest, detectPlatform("Mozilla/5.0 (Windows NT 10.0; ARM64)")).id,
  "windows-arm64-msi",
);
assert.equal(
  selectPrimaryReleaseAsset(manifest, detectPlatform("Mozilla/5.0 (X11; Linux aarch64)")).id,
  "linux-arm64-deb",
);
assert.equal(
  selectPrimaryReleaseAsset(
    manifest,
    detectPlatform("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"),
  ).id,
  "macos-universal-pkg",
);
assert.match(releaseWorkflow, /runner: ubuntu-24\.04-arm[\s\S]*architecture: arm64/);
assert.match(releaseWorkflow, /runner: windows-11-arm[\s\S]*architecture: arm64/);
assert.match(releaseWorkflow, /runner: macos-14[\s\S]*architecture: arm64/);
assert.match(releaseWorkflow, /runner: macos-15-intel[\s\S]*architecture: x64/);
assert.match(
  releaseWorkflow,
  /package-windows:[\s\S]*runs-on: windows-2025[\s\S]*azure\/artifact-signing-action@v2/,
);
assert.match(
  releaseWorkflow,
  /build-windows:[\s\S]*cmake --install build --prefix build\/windows-stage[\s\S]*package-windows:/,
);
assert.doesNotMatch(
  releaseWorkflow.match(/build-windows:[\s\S]*?(?=\n  package-windows:)/)?.[0] ?? "",
  /artifact-signing-action/,
);
assert.match(
  releaseWorkflow,
  /CPACK_INSTALLED_DIRECTORIES=\$PWD\/windows-build\/windows-stage;\/[\s\S]*CPACK_INSTALL_CMAKE_PROJECTS=/,
);
assert.match(releaseWorkflow, /lipo -create/);
assert.match(releaseWorkflow, /lipo -verify_arch arm64 x86_64/);
assert.match(releaseWorkflow, /CPACK_WIX_ARCHITECTURE/);
assert.match(releaseWorkflow, /prepare-release-assets\.sh/);
assert.match(releaseWorkflow, /workflow_dispatch:/);
assert.match(releaseWorkflow, /github\.ref_type == 'tag'/);
assert.match(releaseWorkflow, /gh release create[\s\S]*--draft/);
assert.match(releaseWorkflow, /gh release edit[\s\S]*--draft=false/);
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
    const armTarRootDirectory = path.join(packageDirectory, "dynlex-linux-arm64");
    fs.mkdirSync(path.join(tarRootDirectory, "bin"), { recursive: true });
    fs.mkdirSync(path.join(armTarRootDirectory, "bin"), { recursive: true });
    fs.mkdirSync(mockBinDirectory);

    const writeExecutable = (filePath, contents) => {
      fs.writeFileSync(filePath, contents);
      fs.chmodSync(filePath, 0o755);
    };
    writeExecutable(
      path.join(tarRootDirectory, "bin/dynlex"),
      "#!/bin/sh\n[ \"$1\" = \"--help\" ]\n",
    );
    writeExecutable(
      path.join(armTarRootDirectory, "bin/dynlex"),
      "#!/bin/sh\n[ \"$1\" = \"--help\" ]\n",
    );
    writeExecutable(path.join(mockBinDirectory, "dpkg"), "#!/bin/sh\nexit 0\n");
    writeExecutable(
      path.join(mockBinDirectory, "id"),
      "#!/bin/sh\n[ \"$1\" = \"-u\" ] && printf '0\\n'\n",
    );
    writeExecutable(path.join(mockBinDirectory, "dynlex"), "#!/bin/sh\nexit 0\n");
    writeExecutable(
      path.join(mockBinDirectory, "uname"),
      `#!/bin/sh
case "$1" in
  -s) printf 'Linux\\n' ;;
  -m) printf '%s\\n' "$DYNLEX_TEST_MACHINE" ;;
  *) exit 2 ;;
esac
`,
    );

    const tarPackage = path.join(packageDirectory, "dynlex-linux-x64.tar.gz");
    const armTarPackage = path.join(packageDirectory, "dynlex-linux-arm64.tar.gz");
    for (const [archive, root] of [
      [tarPackage, "dynlex-linux-x64"],
      [armTarPackage, "dynlex-linux-arm64"],
    ]) {
      const tarResult = spawnSync(
        "tar",
        ["-czf", archive, "-C", packageDirectory, root],
        { encoding: "utf8" },
      );
      assert.equal(tarResult.status, 0, tarResult.stderr);
    }

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
      DYNLEX_TEST_MACHINE: "x86_64",
      PATH: `${mockBinDirectory}:${process.env.PATH}`,
    };
    const localDebResult = spawnSync("sh", [installScript, debPackage], {
      cwd: projectDirectory,
      encoding: "utf8",
      env: mockEnvironment,
    });
    assert.equal(localDebResult.status, 0, localDebResult.stdout + localDebResult.stderr);

    const checksumLines = [tarPackage, armTarPackage].map((packagePath) => {
      const checksum = spawnSync("sha256sum", [packagePath], { encoding: "utf8" });
      assert.equal(checksum.status, 0, checksum.stderr);
      const digest = checksum.stdout.trim().split(/\s+/)[0];
      return `${digest}  ${path.basename(packagePath)}`;
    });
    const checksumPath = path.join(temporaryDirectory, "fixture-SHA256SUMS");
    fs.writeFileSync(checksumPath, `${checksumLines.join("\n")}\n`);
    writeExecutable(
      path.join(mockBinDirectory, "curl"),
      `#!/bin/sh
case "$2" in
  */release-manifest.txt) cp "$DYNLEX_TEST_MANIFEST" "$4" ;;
  */dynlex-linux-x64.tar.gz) cp "$DYNLEX_TEST_TAR_X64" "$4" ;;
  */dynlex-linux-arm64.tar.gz) cp "$DYNLEX_TEST_TAR_ARM64" "$4" ;;
  */SHA256SUMS) cp "$DYNLEX_TEST_CHECKSUMS" "$4" ;;
  *) exit 2 ;;
esac
`,
    );

    for (const [machine, architecture] of [
      ["x86_64", "x64"],
      ["aarch64", "arm64"],
    ]) {
      const onlinePrefix = path.join(
        temporaryDirectory,
        `online-${architecture}-tar-prefix`,
      );
      const onlineTarResult = spawnSync(
        "sh",
        [installScript, "--format", "tar", "--prefix", onlinePrefix],
        {
          cwd: projectDirectory,
          encoding: "utf8",
          env: {
            ...mockEnvironment,
            DYNLEX_TEST_MACHINE: machine,
            DYNLEX_TEST_MANIFEST: manifestPath,
            DYNLEX_TEST_TAR_X64: tarPackage,
            DYNLEX_TEST_TAR_ARM64: armTarPackage,
            DYNLEX_TEST_CHECKSUMS: checksumPath,
          },
        },
      );
      assert.equal(
        onlineTarResult.status,
        0,
        onlineTarResult.stdout + onlineTarResult.stderr,
      );
      assert.ok(fs.existsSync(path.join(onlinePrefix, "bin/dynlex")));
    }
  }
} finally {
  fs.rmSync(temporaryDirectory, { recursive: true, force: true });
}

console.log("Release artifacts, downloads, and checksums share one manifest.");
