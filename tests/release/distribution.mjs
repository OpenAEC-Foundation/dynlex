import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import {
  detectPlatform,
  extractReleaseManifest,
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
const linuxInstaller = fs.readFileSync(
  path.join(projectDirectory, "web/install.sh"),
  "utf8",
);
const nativeCodegen = fs.readFileSync(
  path.join(projectDirectory, "src/cpp/compiler/codegen/native.cpp"),
  "utf8",
);
const macosDependencyStager = fs.readFileSync(
  path.join(projectDirectory, "scripts/stage-macos-dependencies.sh"),
  "utf8",
);
const buildDependencyInstaller = fs.readFileSync(
  path.join(projectDirectory, "scripts/install.sh"),
  "utf8",
);
const windowsDependencyInstaller = fs.readFileSync(
  path.join(projectDirectory, "scripts/install.ps1"),
  "utf8",
);
const llvmToolchain = fs.readFileSync(
  path.join(projectDirectory, "scripts/llvm_toolchain.sh"),
  "utf8",
);
const llvmMingwToolchain = fs.readFileSync(
  path.join(projectDirectory, "scripts/llvm-mingw-toolchain.ps1"),
  "utf8",
);
const windowsPeVerifier = fs.readFileSync(
  path.join(projectDirectory, "scripts/verify-windows-pe-files.ps1"),
  "utf8",
);
const vcpkgManifest = JSON.parse(
  fs.readFileSync(path.join(projectDirectory, "vcpkg.json"), "utf8"),
);
const windowsVcpkgTriplets = ["x64", "arm64"].map((architecture) =>
  fs.readFileSync(
    path.join(
      projectDirectory,
      "cmake/vcpkg-triplets",
      `${architecture}-windows-static-crt.cmake`,
    ),
    "utf8",
  )
);

assert.equal(manifest.schema, 1);
assert.equal(manifest.repository, "OpenAEC-Foundation/dynlex");
assert.equal(
  extractReleaseManifest(
    `Release notes.\n\n<!-- dynlex-release-manifest\n${manifestText.trimEnd()}\n-->\n`,
  ),
  manifestText.trimEnd(),
);
assert.throws(
  () => extractReleaseManifest("Release notes without a manifest."),
  /exactly one embedded release manifest/,
);
assert.throws(
  () => extractReleaseManifest(
    `<!-- dynlex-release-manifest\n${manifestText.trimEnd()}\n-->\n`
      + `<!-- dynlex-release-manifest\n${manifestText.trimEnd()}\n-->`,
  ),
  /exactly one embedded release manifest/,
);
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
assert.throws(
  () => detectPlatform("Mozilla/5.0 (Linux; Android 16; Pixel 10 Pro)"),
  /mobile operating systems/,
);
assert.throws(
  () => detectPlatform("Mozilla/5.0 (iPhone; CPU iPhone OS 19_0 like Mac OS X)"),
  /mobile operating systems/,
);
assert.throws(
  () => detectPlatform(
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15) Version/19.0 Mobile/15E148 Safari/604.1",
    "x86",
    5,
  ),
  /mobile operating systems/,
);
assert.throws(
  () => detectPlatform("Mozilla/5.0 (X11; CrOS x86_64 16093.68.0)"),
  /ChromeOS/,
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
assert.match(releaseWorkflow, /runner: macos-15[\s\S]*architecture: arm64/);
assert.doesNotMatch(releaseWorkflow, /macos-14/);
assert.match(releaseWorkflow, /runner: macos-15-intel[\s\S]*architecture: x64/);
assert.match(
  releaseWorkflow,
  /package-windows:[\s\S]*runs-on: windows-2025[\s\S]*azure\/artifact-signing-action@v2/,
);
assert.match(
  releaseWorkflow,
  /build-windows:[\s\S]*cmake --install build --prefix build\/windows-stage[\s\S]*package-windows:/,
);
assert.match(
  releaseWorkflow,
  /build-windows:[\s\S]*stage-windows-toolchain\.ps1[\s\S]*package-windows:/,
);
assert.doesNotMatch(
  releaseWorkflow.match(/build-windows:[\s\S]*?(?=\n  package-windows:)/)?.[0] ?? "",
  /artifact-signing-action/,
);
assert.match(
  releaseWorkflow,
  /CPACK_INSTALLED_DIRECTORIES=\$PWD\/windows-build\/windows-stage;\/[\s\S]*CPACK_INSTALL_CMAKE_PROJECTS=/,
);
assert.match(
  releaseWorkflow,
  /WIX_PATCH_FILE="\$PWD\/cmake\/windows-installer-upgrades\.xml"[\s\S]*CPACK_WIX_PATCH_FILE=\$WIX_PATCH_FILE/,
);
assert.match(releaseWorkflow, /lipo -create/);
assert.match(releaseWorkflow, /lipo -verify_arch arm64 x86_64/);
assert.match(releaseWorkflow, /stage-macos-dependencies\.sh/);
assert.match(
  releaseWorkflow,
  /cmp[\s\S]*dependencies\/dependency-manifest\.txt[\s\S]*dependencies\/dependency-manifest\.txt/,
);
assert.match(releaseWorkflow, /--scripts "\$PWD\/scripts\/macos-installer"/);
assert.match(
  releaseWorkflow,
  /files-folder-filter: dll,exe[\s\S]*files-folder-recurse: true/,
);
assert.match(
  releaseWorkflow,
  /Verify Windows payload signatures[\s\S]*Get-ChildItem[\s\S]*"\*\.exe", "\*\.dll"[\s\S]*verify-windows-signature\.ps1/,
);
assert.match(
  releaseWorkflow,
  /build\/windows-stage[\s\S]*verify-windows-pe-files\.ps1/,
);
assert.match(windowsPeVerifier, /verify-executable-architecture\.py/);
assert.match(windowsPeVerifier, /verify-windows-runtime-dependencies\.py/);
assert.match(
  releaseWorkflow,
  /smoke-windows:[\s\S]*PATH = "\$env:SystemRoot\\System32;\$env:SystemRoot"[\s\S]*graphics_window_should_close_boolean/,
);
assert.match(
  releaseWorkflow,
  /smoke-macos:[\s\S]*PATH=\/usr\/bin:\/bin:\/usr\/sbin:\/sbin[\s\S]*graphics_window_should_close_boolean/,
);
assert.match(
  releaseWorkflow,
  /smoke-linux:[\s\S]*graphics_window_should_close_boolean/,
);
for (const smokeJob of ["smoke-linux", "smoke-windows", "smoke-macos"]) {
  assert.match(
    releaseWorkflow,
    new RegExp(`${smokeJob}:[\\s\\S]*font_link_smoke\\.dl`),
  );
}
assert.match(
  releaseWorkflow,
  /smoke-linux:[\s\S]*font_render_smoke\.dl[\s\S]*xvfb-run[\s\S]*font-render/,
);
assert.match(
  releaseWorkflow,
  /smoke-linux:[\s\S]*sudo apt-get install --yes xvfb[\s\S]*mouse_input_smoke\.dl[\s\S]*xvfb-run[\s\S]*mouse-input/,
);
assert.match(
  releaseWorkflow,
  /smoke-linux:[\s\S]*graphics_scroll_callback_smoke\.dl[\s\S]*xvfb-run[\s\S]*graphics-scroll-callback/,
);
assert.match(
  releaseWorkflow,
  /smoke-linux:[\s\S]*graphics_window_application_state_smoke\.dl[\s\S]*xvfb-run[\s\S]*graphics-window-application-state/,
);
assert.match(releaseWorkflow, /CPACK_WIX_ARCHITECTURE/);
assert.match(releaseWorkflow, /prepare-release-assets\.sh/);
assert.match(releaseWorkflow, /workflow_dispatch:/);
assert.match(releaseWorkflow, /github\.ref_type == 'tag'/);
assert.match(releaseWorkflow, /gh release create[\s\S]*--draft/);
assert.match(releaseWorkflow, /gh release edit[\s\S]*--draft=false/);
assert.match(releaseWorkflow, /--jq \.immutable/);
assert.match(cmakeConfiguration, /CPACK_PACKAGE_HOMEPAGE_URL "https:\/\/dynlex\.com"/);
assert.match(cmakeConfiguration, /CPACK_DEBIAN_PACKAGE_DEPENDS/);
assert.doesNotMatch(cmakeConfiguration, /johnheikens\/DynLex/i);
assert.match(linuxInstaller, /linux_compile_dependencies_ready/);
assert.match(linuxInstaller, /cc -x c - -lglfw -lfreetype -lGL/);
assert.match(linuxInstaller, /pacman -Syu --needed --noconfirm/);
assert.doesNotMatch(linuxInstaller, /pacman -Sy(?:\s|$)/);
assert.match(buildDependencyInstaller, /pacman -Syu --needed --noconfirm/);
assert.doesNotMatch(buildDependencyInstaller, /pacman -Sy(?:\s|$)/);
for (const packageManager of ["apt-get", "dnf", "pacman", "zypper"]) {
  assert.match(linuxInstaller, new RegExp(`command -v ${packageManager}`));
}
assert.equal(
  vcpkgManifest["builtin-baseline"],
  "56bb2411609227288b70117ead2c47585ba07713",
);
assert.deepEqual(
  [...vcpkgManifest.dependencies].sort(),
  ["freetype", "glfw3", "nlohmann-json"],
);
assert.match(windowsDependencyInstaller, /metadata\\VCPKG_TOOLCHAIN/);
assert.match(windowsDependencyInstaller, /--branch \$metadata\.release/);
assert.match(windowsDependencyInstaller, /\$actualCommit -ne \$metadata\.commit/);
assert.match(
  windowsDependencyInstaller,
  /\$visualStudioToolchain = Ensure-VisualStudioCppToolchain\s+`\s+-TargetArchitecture \$dependencyArchitecture/,
);
assert.match(
  windowsDependencyInstaller,
  /Enter-VisualStudioCppEnvironment[\s\S]*VsDevCmd\.bat/,
);
assert.match(windowsDependencyInstaller, /call "\{0\}" -no_logo -arch=\{1\}/);
assert.match(
  windowsDependencyInstaller,
  /\$commandLine = [\s\S]*\$developerCommand,[\s\S]*\$developerArchitecture/,
);
assert.match(
  windowsDependencyInstaller,
  /@\("INCLUDE", "LIB", "LIBPATH", "VSCMD_VER"\)/,
);
assert.match(windowsDependencyInstaller, /Add-GitHubPathIfPresent -PathValue \$pathEntry/);
for (const compilerVariable of [
  "DYNLEX_LLVM_BOOTSTRAP_CC",
  "DYNLEX_LLVM_BOOTSTRAP_CXX",
]) {
  assert.match(
    windowsDependencyInstaller,
    new RegExp(`Add-Content -Path \\$env:GITHUB_ENV -Value "${compilerVariable}=`),
  );
  assert.match(llvmToolchain, new RegExp(`\\$\\{${compilerVariable}:-\\}`));
}
assert.match(
  windowsDependencyInstaller,
  /Microsoft\.VisualStudio\.Workload\.VCTools/,
);
assert.match(windowsDependencyInstaller, /Find-WindowsSdk/);
assert.match(windowsDependencyInstaller, /windows-static-crt/);
for (const triplet of windowsVcpkgTriplets) {
  assert.match(triplet, /VCPKG_CRT_LINKAGE static/);
  assert.match(triplet, /VCPKG_LIBRARY_LINKAGE dynamic/);
  assert.match(triplet, /VCPKG_BUILD_TYPE release/);
}
assert.match(cmakeConfiguration, /CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded/);
assert.match(
  cmakeConfiguration,
  /DYNLEX_WINDOWS_RUNTIME_ARCHIVER[\s\S]*llvm-ar\.exe/,
);
assert.match(
  cmakeConfiguration,
  /add_custom_target\(\s*dynlex_runtime ALL[\s\S]*DYNLEX_WINDOWS_RUNTIME_ARCHIVE/,
);
assert.match(
  cmakeConfiguration,
  /install\(FILES \$\{DYNLEX_WINDOWS_RUNTIME_ARCHIVE\}[\s\S]*CMAKE_INSTALL_LIBDIR\}\/dynlex/,
);
assert.match(
  llvmToolchain,
  /CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded/,
);
assert.match(llvmMingwToolchain, /"include\\windows\.h"/);
assert.match(llvmMingwToolchain, /"include\\sys\\types\.h"/);
assert.match(llvmMingwToolchain, /Join-Path \$extractedRoot "include\\\*"/);
assert.match(
  llvmMingwToolchain,
  /Join-Path \$extractedRoot "include\\\*"\)[\s\S]*?-Recurse/,
);
assert.doesNotMatch(
  llvmMingwToolchain,
  /\$TargetArchitecture-w64-mingw32\\include/,
);
assert.match(macosDependencyStager, /^#!\/bin\/bash/);
assert.doesNotMatch(macosDependencyStager, /\b(?:declare -A|mapfile)\b/);
assert.match(macosDependencyStager, /dependency-manifest\.txt/);
assert.match(macosDependencyStager, /formula_sha256/);
assert.match(nativeCodegen, /DYNLEX_WINDOWS_TARGET_TRIPLE/);
assert.match(nativeCodegen, /DYNLEX_WINDOWS_TOOLCHAIN_INSTALL_DIR/);
assert.match(nativeCodegen, /copyRuntimeLibraries/);

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
    const curlLogPath = path.join(temporaryDirectory, "curl-requests.log");
    fs.writeFileSync(checksumPath, `${checksumLines.join("\n")}\n`);
    writeExecutable(
      path.join(mockBinDirectory, "curl"),
      `#!/bin/sh
if [ "$1" = "-fsS" ]; then
  printf '%s\\n' "$6" >> "$DYNLEX_TEST_CURL_LOG"
  printf 'https://github.com/OpenAEC-Foundation/dynlex/releases/download/%s/release-manifest.txt' "$DYNLEX_TEST_RELEASE_TAG"
  exit 0
fi
printf '%s\\n' "$2" >> "$DYNLEX_TEST_CURL_LOG"
case "$2" in
  */releases/download/0.0.1/release-manifest.txt) cp "$DYNLEX_TEST_MANIFEST" "$4" ;;
  */releases/download/0.0.1/dynlex-linux-x64.tar.gz) cp "$DYNLEX_TEST_TAR_X64" "$4" ;;
  */releases/download/0.0.1/dynlex-linux-arm64.tar.gz) cp "$DYNLEX_TEST_TAR_ARM64" "$4" ;;
  */releases/download/0.0.1/SHA256SUMS) cp "$DYNLEX_TEST_CHECKSUMS" "$4" ;;
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
            DYNLEX_TEST_CURL_LOG: curlLogPath,
            DYNLEX_TEST_RELEASE_TAG: "0.0.1",
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

    const requestedUrls = fs.readFileSync(curlLogPath, "utf8").trim().split("\n");
    assert.equal(
      requestedUrls.filter((url) => url.includes("/releases/latest/download/")).length,
      2,
      "Each installer run must resolve latest exactly once",
    );
    assert.ok(
      requestedUrls
        .filter((url) => !url.includes("/releases/latest/download/"))
        .every((url) => url.includes("/releases/download/0.0.1/")),
      "Manifest, package, and checksums must use the one pinned release tag",
    );
  }
} finally {
  fs.rmSync(temporaryDirectory, { recursive: true, force: true });
}

console.log("Release artifacts, downloads, and checksums share one manifest.");
