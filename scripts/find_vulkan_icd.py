#!/usr/bin/env python3
from __future__ import annotations

import os
import platform
import subprocess
import sys
from collections.abc import Iterable, Mapping
from pathlib import Path


ARCHITECTURE_NAMES = {
    "aarch64": {"aarch64", "arm64"},
    "amd64": {"amd64", "x86_64"},
    "arm64": {"aarch64", "arm64"},
    "armv7l": {"arm", "armhf", "armv7", "armv7l"},
    "i386": {"i386", "i486", "i586", "i686", "x86"},
    "i686": {"i386", "i486", "i586", "i686", "x86"},
    "ppc64le": {"ppc64le"},
    "riscv64": {"riscv64"},
    "s390x": {"s390x"},
    "x86_64": {"amd64", "x86_64"},
}


def linux_manifest_directories() -> list[Path]:
    suffix = Path("vulkan/icd.d")
    config_home = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    config_directories = os.environ.get("XDG_CONFIG_DIRS") or "/etc/xdg"
    data_home = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local/share")
    data_directories = os.environ.get("XDG_DATA_DIRS") or os.pathsep.join(
        ["/usr/local/share", "/usr/share"]
    )
    directories = [Path(config_home) / suffix]
    directories.extend(Path(directory) / suffix for directory in config_directories.split(os.pathsep))
    directories.extend([Path("/usr/local/etc") / suffix, Path("/etc") / suffix])
    directories.append(Path(data_home) / suffix)
    directories.extend(Path(directory) / suffix for directory in data_directories.split(os.pathsep))
    return directories


def vulkan_driver_environment(
    manifest: Path,
    base_environment: Mapping[str, str] | None = None,
) -> dict[str, str]:
    environment = dict(os.environ if base_environment is None else base_environment)
    path = str(manifest)
    environment["VK_DRIVER_FILES"] = path
    environment["VK_ICD_FILENAMES"] = path
    return environment


def homebrew_formula_prefix(formula: str) -> Path:
    try:
        result = subprocess.run(
            ["brew", "--prefix", formula],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError("Homebrew is not installed") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip()
        message = f"Homebrew could not locate the {formula} formula"
        raise RuntimeError(f"{message}: {detail}" if detail else message) from error

    lines = result.stdout.splitlines()
    if len(lines) != 1 or not lines[0]:
        raise RuntimeError(f"Homebrew returned an invalid prefix for the {formula} formula")
    prefix = Path(lines[0]).expanduser()
    if not prefix.is_absolute():
        raise RuntimeError(f"Homebrew returned a relative prefix for the {formula} formula: {prefix}")
    return prefix.resolve()


def configured_vulkan_icd() -> Path | None:
    requested = os.environ.get("DYNLEX_TEST_VULKAN_ICD")
    if not requested:
        return None
    manifest = Path(requested).expanduser().resolve()
    if not manifest.is_file():
        raise RuntimeError(f"configured Vulkan driver manifest does not exist: {manifest}")
    return manifest


def find_lavapipe_icd(
    directories: Iterable[Path] | None = None,
    machine: str | None = None,
) -> Path:
    architecture = (platform.machine() if machine is None else machine).lower()
    architecture_names = ARCHITECTURE_NAMES.get(architecture, {architecture})
    unqualified: list[Path] = []
    architecture_matches: list[Path] = []
    other_candidates: list[Path] = []
    manifest_directories = linux_manifest_directories() if directories is None else directories
    for directory in manifest_directories:
        for manifest in sorted(directory.glob("lvp_icd*.json")):
            if manifest.name == "lvp_icd.json":
                unqualified.append(manifest)
                continue
            qualifier = manifest.name.removeprefix("lvp_icd.").removesuffix(".json").lower()
            if qualifier in architecture_names:
                architecture_matches.append(manifest)
            else:
                other_candidates.append(manifest)

    if unqualified:
        return unqualified[0].resolve()
    if len(architecture_matches) == 1:
        return architecture_matches[0].resolve()
    if len(architecture_matches) > 1:
        candidates = ", ".join(str(path) for path in architecture_matches)
        raise RuntimeError(f"multiple Lavapipe ICD manifests match {architecture}: {candidates}")
    if other_candidates:
        candidates = ", ".join(str(path) for path in other_candidates)
        raise RuntimeError(f"Lavapipe ICD manifests were found, but none match {architecture}: {candidates}")
    raise RuntimeError("no Lavapipe ICD manifest was found")


def find_moltenvk_icd(prefix: Path | None = None) -> Path:
    moltenvk_prefix = homebrew_formula_prefix("molten-vk") if prefix is None else prefix
    manifest = moltenvk_prefix / "etc/vulkan/icd.d/MoltenVK_icd.json"
    if not manifest.is_file():
        raise RuntimeError(f"the Homebrew MoltenVK driver manifest does not exist: {manifest}")
    return manifest.resolve()


def find_vulkan_icd(
    platform_name: str | None = None,
    directories: Iterable[Path] | None = None,
    machine: str | None = None,
    moltenvk_prefix: Path | None = None,
) -> Path:
    configured = configured_vulkan_icd()
    if configured is not None:
        return configured

    current_platform = sys.platform if platform_name is None else platform_name
    if current_platform.startswith("linux"):
        return find_lavapipe_icd(directories, machine)
    if current_platform == "darwin":
        return find_moltenvk_icd(moltenvk_prefix)
    raise RuntimeError(f"Vulkan test driver discovery does not support {current_platform}")


if __name__ == "__main__":
    try:
        print(find_vulkan_icd())
    except RuntimeError as error:
        raise SystemExit(str(error)) from error
