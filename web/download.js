const fallbackReleaseUrl = "https://github.com/OpenAEC-Foundation/dynlex/releases/latest";

function requireSingleRecord(currentValue, recordName) {
  if (currentValue !== null) {
    throw new Error(`duplicate ${recordName} record`);
  }
}

export function parseReleaseManifest(text) {
  if (typeof text !== "string") {
    throw new TypeError("release manifest must be text");
  }

  let schema = null;
  let repository = null;
  const assets = [];
  const assetIds = new Set();
  const assetNames = new Set();

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (line === "" || line.startsWith("#")) {
      continue;
    }

    const fields = line.split(/\s+/);
    if (fields[0] === "schema") {
      requireSingleRecord(schema, "schema");
      if (fields.length !== 2 || !/^[1-9][0-9]*$/.test(fields[1])) {
        throw new Error("invalid release manifest schema record");
      }
      schema = Number(fields[1]);
      continue;
    }

    if (fields[0] === "repository") {
      requireSingleRecord(repository, "repository");
      if (fields.length !== 2 || !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(fields[1])) {
        throw new Error("invalid release manifest repository record");
      }
      repository = fields[1];
      continue;
    }

    if (fields[0] !== "asset" || fields.length !== 6) {
      throw new Error("invalid release manifest record");
    }

    const [, id, os, architectureList, format, name] = fields;
    if (!/^[a-z0-9][a-z0-9-]*$/.test(id)) {
      throw new Error("invalid release asset identifier");
    }
    if (!["linux", "windows", "macos"].includes(os)) {
      throw new Error("invalid release asset operating system");
    }
    const architectures = architectureList.split(",");
    if (
      architectures.length === 0
      || architectures.some((architecture) => !["arm64", "x64"].includes(architecture))
      || new Set(architectures).size !== architectures.length
    ) {
      throw new Error("invalid release asset architecture list");
    }
    if (!["deb", "tar.gz", "msi", "pkg"].includes(format)) {
      throw new Error("invalid release asset format");
    }
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(name)) {
      throw new Error("invalid release asset name");
    }
    if (assetIds.has(id)) {
      throw new Error(`duplicate release asset identifier: ${id}`);
    }
    if (assetNames.has(name)) {
      throw new Error(`duplicate release asset name: ${name}`);
    }
    assetIds.add(id);
    assetNames.add(name);
    assets.push({ id, os, architectures, format, name });
  }

  if (schema !== 1) {
    throw new Error("unsupported release manifest schema");
  }
  if (repository === null) {
    throw new Error("release manifest is missing its repository");
  }
  if (assets.length === 0) {
    throw new Error("release manifest contains no assets");
  }

  return { schema, repository, assets };
}

export function selectReleaseAssets(releaseAssets, manifest) {
  if (!Array.isArray(releaseAssets)) {
    throw new TypeError("GitHub release assets must be an array");
  }

  const selected = new Map();
  for (const expectedAsset of manifest.assets) {
    const matches = releaseAssets.filter((asset) => asset && asset.name === expectedAsset.name);
    if (matches.length === 0) {
      throw new Error(`missing release asset: ${expectedAsset.name}`);
    }
    if (matches.length !== 1) {
      throw new Error(`duplicate release asset: ${expectedAsset.name}`);
    }
    const [match] = matches;
    if (typeof match.browser_download_url !== "string" || match.browser_download_url === "") {
      throw new Error(`release asset has no download URL: ${expectedAsset.name}`);
    }
    selected.set(expectedAsset.id, match);
  }
  return selected;
}

export function detectPlatform(userAgent) {
  if (typeof userAgent !== "string") {
    throw new TypeError("user agent must be text");
  }

  if (/Windows/i.test(userAgent)) {
    if (/ARM64|aarch64/i.test(userAgent)) {
      throw new Error("unsupported Windows architecture: arm64");
    }
    return {
      os: "windows",
      architecture: "x64",
      label: "Windows",
      primaryAssetId: "windows-x64-msi",
    };
  }

  if (/Macintosh|Mac OS/i.test(userAgent)) {
    return {
      os: "macos",
      architecture: "universal",
      label: "macOS",
      primaryAssetId: "macos-universal-pkg",
    };
  }

  if (/Linux/i.test(userAgent)) {
    if (/aarch64|arm64/i.test(userAgent)) {
      throw new Error("unsupported Linux architecture: arm64");
    }
    return {
      os: "linux",
      architecture: "x64",
      label: "Linux",
      primaryAssetId: "linux-x64-deb",
    };
  }

  throw new Error("unsupported operating system");
}

async function loadRelease() {
  const manifestResponse = await fetch("release-manifest.txt", { cache: "no-cache" });
  if (!manifestResponse.ok) {
    throw new Error("release manifest fetch failed");
  }
  const manifest = parseReleaseManifest(await manifestResponse.text());

  const releaseResponse = await fetch(
    `https://api.github.com/repos/${manifest.repository}/releases/latest`,
    { headers: { Accept: "application/vnd.github+json" } },
  );
  if (!releaseResponse.ok) {
    throw new Error("release metadata fetch failed");
  }
  const release = await releaseResponse.json();
  return {
    manifest,
    selectedAssets: selectReleaseAssets(release.assets, manifest),
  };
}

function configureDownloadPage() {
  const title = document.getElementById("hero-title");
  const primary = document.getElementById("primary-download");
  const status = document.getElementById("status-line");
  const alternateLinks = new Map([
    ["windows-x64-msi", document.getElementById("alt-windows")],
    ["macos-universal-pkg", document.getElementById("alt-macos")],
    ["linux-x64-deb", document.getElementById("alt-linux-deb")],
    ["linux-x64-tar", document.getElementById("alt-linux-tar")],
  ]);

  let platform;
  try {
    platform = detectPlatform(navigator.userAgent || "");
  } catch (error) {
    console.error("DynLex platform detection failed.", error);
    title.textContent = "Download DynLex";
    status.textContent = "Automatic installation is unavailable for this platform.";
    return;
  }

  title.textContent = `Download DynLex for ${platform.label}`;
  primary.href = fallbackReleaseUrl;
  primary.querySelector("span").textContent = `Download for ${platform.label}`;
  status.textContent = `Preparing the ${platform.label} download…`;

  loadRelease().then(({ selectedAssets }) => {
    for (const [assetId, link] of alternateLinks) {
      link.href = selectedAssets.get(assetId).browser_download_url;
    }

    const primaryUrl = selectedAssets.get(platform.primaryAssetId).browser_download_url;
    primary.href = primaryUrl;
    status.textContent = `Your ${platform.label} download is starting.`;
    setTimeout(() => {
      window.location.assign(primaryUrl);
    }, 550);
  }).catch((error) => {
    console.error("DynLex release resolution failed.", error);
    status.textContent = "An error occurred while preparing the download. Check the browser log.";
  });
}

if (typeof document !== "undefined") {
  configureDownloadPage();
}
