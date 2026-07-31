const releaseRepository = "OpenAEC-Foundation/dynlex";
const fallbackReleaseUrl = `https://github.com/${releaseRepository}/releases/latest`;

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

function normalizeArchitecture(architecture) {
  if (typeof architecture !== "string") {
    return null;
  }
  if (/arm|aarch64/i.test(architecture)) {
    return "arm64";
  }
  if (/x86|x64|amd64/i.test(architecture)) {
    return "x64";
  }
  return null;
}

export function detectPlatform(userAgent, clientArchitecture = null) {
  if (typeof userAgent !== "string") {
    throw new TypeError("user agent must be text");
  }

  const detectedArchitecture =
    normalizeArchitecture(clientArchitecture)
    ?? normalizeArchitecture(userAgent);

  if (/Windows/i.test(userAgent)) {
    return {
      os: "windows",
      architecture: detectedArchitecture ?? "x64",
      label: "Windows",
    };
  }

  if (/Macintosh|Mac OS/i.test(userAgent)) {
    return {
      os: "macos",
      architecture: detectedArchitecture === "arm64" ? "arm64" : "unknown",
      label: "macOS",
    };
  }

  if (/Linux/i.test(userAgent)) {
    return {
      os: "linux",
      architecture: detectedArchitecture ?? "x64",
      label: "Linux",
    };
  }

  throw new Error("unsupported operating system");
}

export function selectPrimaryReleaseAsset(manifest, platform) {
  const preferredFormat = {
    linux: "deb",
    macos: "pkg",
    windows: "msi",
  }[platform.os];
  if (preferredFormat === undefined) {
    throw new Error(`unsupported operating system: ${platform.os}`);
  }

  const matchingAssets = manifest.assets.filter((asset) => {
    if (asset.os !== platform.os || asset.format !== preferredFormat) {
      return false;
    }
    if (platform.architecture === "unknown") {
      return asset.architectures.includes("arm64") && asset.architectures.includes("x64");
    }
    return asset.architectures.includes(platform.architecture);
  });

  if (matchingAssets.length === 0) {
    throw new Error(
      `release has no ${platform.os} ${platform.architecture} ${preferredFormat} asset`,
    );
  }
  if (matchingAssets.length !== 1) {
    throw new Error(
      `release has multiple ${platform.os} ${platform.architecture} ${preferredFormat} assets`,
    );
  }
  return matchingAssets[0];
}

function releaseAssetLabel(asset) {
  const operatingSystem = {
    linux: "Linux",
    macos: "macOS",
    windows: "Windows",
  }[asset.os];
  const architecture =
    asset.architectures.length === 2
      ? "Universal"
      : asset.architectures[0] === "arm64"
        ? "ARM64"
        : "x64";
  return `${operatingSystem} ${architecture} (.${asset.format})`;
}

async function detectBrowserPlatform() {
  let clientArchitecture = navigator.platform || "";
  if (navigator.userAgentData?.getHighEntropyValues) {
    const values = await navigator.userAgentData.getHighEntropyValues([
      "architecture",
      "bitness",
    ]);
    clientArchitecture = `${values.architecture ?? ""}${values.bitness ?? ""}`;
  }
  return detectPlatform(navigator.userAgent || "", clientArchitecture);
}

async function loadRelease() {
  const releaseResponse = await fetch(
    `https://api.github.com/repos/${releaseRepository}/releases/latest`,
    { headers: { Accept: "application/vnd.github+json" } },
  );
  if (!releaseResponse.ok) {
    throw new Error("release metadata fetch failed");
  }
  const release = await releaseResponse.json();
  const manifestAssets = release.assets.filter(
    (asset) => asset?.name === "release-manifest.txt",
  );
  if (manifestAssets.length !== 1) {
    throw new Error("release must contain exactly one release manifest");
  }

  const manifestResponse = await fetch(manifestAssets[0].browser_download_url, {
    cache: "no-cache",
  });
  if (!manifestResponse.ok) {
    throw new Error("release manifest fetch failed");
  }
  const manifest = parseReleaseManifest(await manifestResponse.text());
  if (manifest.repository !== releaseRepository) {
    throw new Error("release manifest names an unexpected repository");
  }
  return {
    manifest,
    selectedAssets: selectReleaseAssets(release.assets, manifest),
  };
}

function renderAlternateDownloads(container, manifest, selectedAssets) {
  const links = manifest.assets.map((asset) => {
    const link = document.createElement("a");
    link.className = "download-btn";
    link.href = selectedAssets.get(asset.id).browser_download_url;
    link.textContent = releaseAssetLabel(asset);
    return link;
  });
  container.replaceChildren(...links);
}

async function configureDownloadPage() {
  const title = document.getElementById("hero-title");
  const primary = document.getElementById("primary-download");
  const status = document.getElementById("status-line");
  const alternateDownloads = document.getElementById("alternate-downloads");

  primary.href = fallbackReleaseUrl;
  status.textContent = "Preparing the platform downloads…";

  let release;
  try {
    release = await loadRelease();
    renderAlternateDownloads(
      alternateDownloads,
      release.manifest,
      release.selectedAssets,
    );
  } catch (error) {
    console.error("DynLex release resolution failed.", error);
    title.textContent = "Download DynLex";
    status.textContent = "An error occurred while preparing the download. Check the browser log.";
    return;
  }

  let platform;
  try {
    platform = await detectBrowserPlatform();
  } catch (error) {
    console.info("Automatic platform detection is unavailable.", error);
    title.textContent = "Download DynLex";
    status.textContent = "Choose the download for your platform below.";
    return;
  }

  try {
    title.textContent = `Download DynLex for ${platform.label}`;
    primary.querySelector("span").textContent = `Download for ${platform.label}`;

    const primaryAsset = selectPrimaryReleaseAsset(release.manifest, platform);
    const primaryUrl = release.selectedAssets.get(primaryAsset.id).browser_download_url;
    primary.href = primaryUrl;
    status.textContent = `Your ${platform.label} download is starting.`;
    setTimeout(() => {
      window.location.assign(primaryUrl);
    }, 550);
  } catch (error) {
    console.error("DynLex release resolution failed.", error);
    title.textContent = "Download DynLex";
    status.textContent = "An error occurred while preparing the download. Check the browser log.";
  }
}

if (typeof document !== "undefined") {
  configureDownloadPage();
}
