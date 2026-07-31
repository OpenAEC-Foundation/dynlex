#!/usr/bin/env python3

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


PROJECT_DIRECTORY = Path(__file__).resolve().parents[2]


class WindowsUpgradeMetadataTests(unittest.TestCase):
    def setUp(self) -> None:
        metadata_lines = (
            PROJECT_DIRECTORY / "metadata/WINDOWS_INSTALLER_UPGRADE_CODES"
        ).read_text(encoding="utf-8").splitlines()
        records = [line.split() for line in metadata_lines]
        self.assertTrue(all(len(record) == 2 for record in records))
        self.upgrade_codes = dict(records)
        self.assertEqual(
            self.upgrade_codes,
            {
                "stable": "F975BC5F-429D-4F99-B362-5014E2B93EE9",
                "legacy": "3FDC26E4-AE29-4FEF-BF7E-9F96E4BE1941",
            },
        )

    def test_cpack_uses_upgrade_code_metadata(self) -> None:
        configuration = (PROJECT_DIRECTORY / "CMakeLists.txt").read_text(
            encoding="utf-8",
        )
        self.assertIn(
            'include("${CMAKE_SOURCE_DIR}/cmake/WindowsInstallerUpgradeCodes.cmake")',
            configuration,
        )
        self.assertIn(
            "dynlex_read_windows_installer_upgrade_codes(",
            configuration,
        )
        self.assertIn(
            'set(CPACK_WIX_UPGRADE_GUID "${DYNLEX_WINDOWS_STABLE_UPGRADE_CODE}")',
            configuration,
        )
        self.assertIn(
            '"-dDYNLEX_LEGACY_UPGRADE_CODE=${DYNLEX_WINDOWS_LEGACY_UPGRADE_CODE}"',
            configuration,
        )
        self.assertNotIn(self.upgrade_codes["stable"], configuration)
        self.assertNotIn(self.upgrade_codes["legacy"], configuration)

    def test_patch_removes_the_other_published_installer_family(self) -> None:
        root = ET.parse(
            PROJECT_DIRECTORY / "cmake/windows-installer-upgrades.xml",
        ).getroot()
        fragments = root.findall("./CPackWiXFragment")
        self.assertEqual(len(fragments), 1)
        self.assertEqual(fragments[0].attrib, {"Id": "#PRODUCT"})

        upgrades = fragments[0].findall("./Upgrade")
        self.assertEqual(len(upgrades), 1)
        self.assertEqual(
            upgrades[0].attrib,
            {"Id": "$(var.DYNLEX_LEGACY_UPGRADE_CODE)"},
        )

        versions = upgrades[0].findall("./UpgradeVersion")
        self.assertEqual(len(versions), 1)
        self.assertEqual(
            versions[0].attrib,
            {
                "Minimum": "0.0.0",
                "IncludeMinimum": "yes",
                "Maximum": "$(var.CPACK_PACKAGE_VERSION)",
                "IncludeMaximum": "no",
                "OnlyDetect": "no",
                "Property": "DYNLEX_LEGACY_VERSION_FOUND",
            },
        )


if __name__ == "__main__":
    unittest.main()
