#!/usr/bin/env python3
from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import find_vulkan_icd as vulkan


class VulkanManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        override = mock.patch.dict(os.environ, {"DYNLEX_TEST_VULKAN_ICD": ""})
        override.start()
        self.addCleanup(override.stop)

    def test_configured_manifest_overrides_discovery(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lavapipe-") as temporary_directory:
            root = Path(temporary_directory)
            configured = root / "configured.json"
            configured.touch()
            with mock.patch.dict(os.environ, {"DYNLEX_TEST_VULKAN_ICD": str(configured)}):
                self.assertEqual(
                    vulkan.find_vulkan_icd(platform_name="unsupported"),
                    configured.resolve(),
                )

    def test_empty_xdg_variables_use_loader_defaults(self) -> None:
        with mock.patch.dict(
            os.environ,
            {
                "XDG_CONFIG_HOME": "",
                "XDG_CONFIG_DIRS": "",
                "XDG_DATA_HOME": "",
                "XDG_DATA_DIRS": "",
            },
        ):
            directories = vulkan.linux_manifest_directories()
        self.assertEqual(directories[0], Path.home() / ".config/vulkan/icd.d")
        self.assertEqual(directories[1], Path("/etc/xdg/vulkan/icd.d"))
        self.assertIn(Path("/usr/local/share/vulkan/icd.d"), directories)
        self.assertIn(Path("/usr/share/vulkan/icd.d"), directories)
        self.assertNotIn(Path("vulkan/icd.d"), directories)

    def test_manifest_directories_follow_vulkan_loader_order(self) -> None:
        environment = {
            "XDG_CONFIG_HOME": "/config-home",
            "XDG_CONFIG_DIRS": os.pathsep.join(["/config-first", "/config-second"]),
            "XDG_DATA_HOME": "/data-home",
            "XDG_DATA_DIRS": os.pathsep.join(["/data-first", "/data-second"]),
        }
        with mock.patch.dict(os.environ, environment):
            directories = vulkan.linux_manifest_directories()
        self.assertEqual(
            directories,
            [
                Path("/config-home/vulkan/icd.d"),
                Path("/config-first/vulkan/icd.d"),
                Path("/config-second/vulkan/icd.d"),
                Path("/usr/local/etc/vulkan/icd.d"),
                Path("/etc/vulkan/icd.d"),
                Path("/data-home/vulkan/icd.d"),
                Path("/data-first/vulkan/icd.d"),
                Path("/data-second/vulkan/icd.d"),
            ],
        )

    def test_selected_driver_replaces_both_loader_overrides(self) -> None:
        manifest = Path("/selected/lvp_icd.json")
        environment = {
            "VK_DRIVER_FILES": "/ambient/current.json",
            "VK_ICD_FILENAMES": "/ambient/deprecated.json",
            "UNCHANGED": "value",
        }
        selected = vulkan.vulkan_driver_environment(manifest, environment)
        self.assertEqual(selected["VK_DRIVER_FILES"], str(manifest))
        self.assertEqual(selected["VK_ICD_FILENAMES"], str(manifest))
        self.assertEqual(selected["UNCHANGED"], "value")

    def test_finds_distribution_neutral_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lavapipe-") as temporary_directory:
            root = Path(temporary_directory)
            manifest = root / "lvp_icd.json"
            manifest.touch()
            self.assertEqual(
                vulkan.find_vulkan_icd(
                    platform_name="linux",
                    directories=[root],
                    machine="x86_64",
                ),
                manifest.resolve(),
            )

    def test_finds_architecture_qualified_fedora_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lavapipe-") as temporary_directory:
            root = Path(temporary_directory)
            (root / "lvp_icd.i686.json").touch()
            manifest = root / "lvp_icd.x86_64.json"
            manifest.touch()
            self.assertEqual(
                vulkan.find_vulkan_icd(
                    platform_name="linux",
                    directories=[root],
                    machine="x86_64",
                ),
                manifest.resolve(),
            )

    def test_finds_homebrew_moltenvk_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-moltenvk-") as temporary_directory:
            prefix = Path(temporary_directory)
            manifest = prefix / "etc/vulkan/icd.d/MoltenVK_icd.json"
            manifest.parent.mkdir(parents=True)
            manifest.touch()
            self.assertEqual(
                vulkan.find_vulkan_icd(
                    platform_name="darwin",
                    moltenvk_prefix=prefix,
                ),
                manifest.resolve(),
            )

    def test_uses_the_installed_homebrew_moltenvk_prefix(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-moltenvk-") as temporary_directory:
            prefix = Path(temporary_directory)
            manifest = prefix / "etc/vulkan/icd.d/MoltenVK_icd.json"
            manifest.parent.mkdir(parents=True)
            manifest.touch()
            with mock.patch.object(
                vulkan,
                "homebrew_formula_prefix",
                return_value=prefix,
            ) as find_prefix:
                self.assertEqual(
                    vulkan.find_vulkan_icd(platform_name="darwin"),
                    manifest.resolve(),
                )
            find_prefix.assert_called_once_with("molten-vk")

    def test_rejects_multiple_manifests_for_the_architecture(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lavapipe-") as temporary_directory:
            root = Path(temporary_directory)
            first = root / "first"
            second = root / "second"
            first.mkdir()
            second.mkdir()
            (first / "lvp_icd.x86_64.json").touch()
            (second / "lvp_icd.x86_64.json").touch()
            with self.assertRaisesRegex(RuntimeError, "multiple Lavapipe"):
                vulkan.find_vulkan_icd(
                    platform_name="linux",
                    directories=[first, second],
                    machine="x86_64",
                )

    def test_rejects_a_manifest_for_another_architecture(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lavapipe-") as temporary_directory:
            root = Path(temporary_directory)
            (root / "lvp_icd.aarch64.json").touch()
            with self.assertRaisesRegex(RuntimeError, "none match x86_64"):
                vulkan.find_vulkan_icd(
                    platform_name="linux",
                    directories=[root],
                    machine="x86_64",
                )


if __name__ == "__main__":
    unittest.main()
