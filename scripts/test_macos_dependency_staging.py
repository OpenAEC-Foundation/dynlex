#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
STAGING_SCRIPT = SCRIPT_DIRECTORY / "stage-macos-dependencies.sh"


def write_executable(path: Path, contents: str) -> None:
    path.write_text(contents, encoding="utf-8")
    path.chmod(0o755)


class MacOSDependencyStagingTests(unittest.TestCase):
    def test_stages_a_versioned_dependency_closure_without_bash_4_features(self) -> None:
        script = STAGING_SCRIPT.read_text(encoding="utf-8")
        for unsupported in ("declare -A", "mapfile", "readarray", "${!"):
            self.assertNotIn(unsupported, script)

        with tempfile.TemporaryDirectory(
            prefix="dynlex-macos-dependency-stage-",
        ) as temporary_directory:
            root = Path(temporary_directory)
            homebrew = root / "homebrew"
            mock_bin = root / "bin"
            installation_root = root / "installation"
            mock_bin.mkdir()
            installation_root.mkdir()

            formulas = {
                "glfw": ("3.4", "libglfw.3.dylib"),
                "freetype": ("2.14.3", "libfreetype.6.dylib"),
                "libpng": ("1.6.50", "libpng16.16.dylib"),
                "vulkan-loader": ("1.4.357.0", "libvulkan.1.dylib"),
                "molten-vk": ("1.4.2", "libMoltenVK.dylib"),
            }
            expected_formula_hashes: dict[str, str] = {}
            for formula, (version, library) in formulas.items():
                formula_root = homebrew / "Cellar" / formula / version
                library_directory = formula_root / "lib"
                formula_directory = formula_root / ".brew"
                library_directory.mkdir(parents=True)
                formula_directory.mkdir()
                (library_directory / library).write_bytes(f"{formula}\n".encode())
                formula_definition = f'class {formula.title()} < Formula\nend\n'
                (formula_directory / f"{formula}.rb").write_text(
                    formula_definition,
                    encoding="utf-8",
                )
                (formula_root / "LICENSE").write_text(
                    f"{formula} test license\n",
                    encoding="utf-8",
                )
                expected_formula_hashes[formula] = hashlib.sha256(
                    formula_definition.encode(),
                ).hexdigest()
                opt_link = homebrew / "opt" / formula
                opt_link.parent.mkdir(exist_ok=True)
                opt_link.symlink_to(formula_root)

            molten_manifest = (
                homebrew
                / "Cellar"
                / "molten-vk"
                / formulas["molten-vk"][0]
                / "etc/vulkan/icd.d/MoltenVK_icd.json"
            )
            molten_manifest.parent.mkdir(parents=True)
            molten_manifest.write_text(
                """{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../lib/libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
""",
                encoding="utf-8",
            )

            write_executable(
                mock_bin / "brew",
                """#!/bin/bash
if [[ "$1" != "--prefix" || $# -ne 2 ]]; then
    exit 2
fi
printf '%s/opt/%s\\n' "$MOCK_HOMEBREW" "$2"
""",
            )
            write_executable(
                mock_bin / "otool",
                """#!/bin/bash
if [[ "$1" != "-L" || $# -ne 2 ]]; then
    exit 2
fi
library="$2"
printf '%s:\\n' "$library"
printf '\\t%s (compatibility version 1.0.0, current version 1.0.0)\\n' "$library"
case "$(basename "$library")" in
    libfreetype.6.dylib)
        printf '\\t%s/opt/libpng/lib/libpng16.16.dylib (compatibility version 1.0.0, current version 1.0.0)\\n' "$MOCK_HOMEBREW"
        ;;
esac
printf '\\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\\n'
""",
            )
            write_executable(
                mock_bin / "install_name_tool",
                """#!/bin/bash
printf '%s\\n' "$*" >> "$INSTALL_NAME_TOOL_LOG"
""",
            )

            install_name_tool_log = root / "install-name-tool.log"
            environment = os.environ.copy()
            environment.update({
                "INSTALL_NAME_TOOL_LOG": str(install_name_tool_log),
                "MOCK_HOMEBREW": str(homebrew),
                "PATH": f"{mock_bin}:{environment['PATH']}",
            })
            completed = subprocess.run(
                ["bash", str(STAGING_SCRIPT), str(installation_root)],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(
                completed.returncode,
                0,
                completed.stdout + completed.stderr,
            )

            destination = (
                installation_root / "usr/local/lib/dynlex/dependencies"
            )
            self.assertEqual(
                sorted(path.name for path in destination.glob("*.dylib")),
                [
                    "libMoltenVK.dylib",
                    "libfreetype.dylib",
                    "libglfw.dylib",
                    "libpng16.16.dylib",
                    "libvulkan.dylib",
                ],
            )
            self.assertEqual(
                sorted(path.name for path in (destination / "licenses").iterdir()),
                [
                    "freetype-LICENSE",
                    "glfw-LICENSE",
                    "libpng-LICENSE",
                    "molten-vk-LICENSE",
                    "vulkan-loader-LICENSE",
                ],
            )
            self.assertEqual(
                (destination / "MoltenVK_icd.json").read_text(encoding="utf-8"),
                """{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "./libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
""",
            )
            expected_manifest_lines = {
                (
                    "libfreetype.dylib\tfreetype\t2.14.3\t"
                    f"lib/libfreetype.6.dylib\t{expected_formula_hashes['freetype']}"
                ),
                (
                    "libglfw.dylib\tglfw\t3.4\t"
                    f"lib/libglfw.3.dylib\t{expected_formula_hashes['glfw']}"
                ),
                (
                    "libpng16.16.dylib\tlibpng\t1.6.50\t"
                    f"lib/libpng16.16.dylib\t{expected_formula_hashes['libpng']}"
                ),
                (
                    "libvulkan.dylib\tvulkan-loader\t1.4.357.0\t"
                    "lib/libvulkan.1.dylib\t"
                    f"{expected_formula_hashes['vulkan-loader']}"
                ),
                (
                    "libMoltenVK.dylib\tmolten-vk\t1.4.2\t"
                    "lib/libMoltenVK.dylib\t"
                    f"{expected_formula_hashes['molten-vk']}"
                ),
                (
                    "MoltenVK_icd.json\tmolten-vk\t1.4.2\t"
                    "etc/vulkan/icd.d/MoltenVK_icd.json\t"
                    f"{expected_formula_hashes['molten-vk']}"
                ),
            }
            manifest_lines = (
                destination / "dependency-manifest.txt"
            ).read_text(encoding="utf-8").splitlines()
            self.assertEqual(manifest_lines[:2], [
                "schema 2",
                "fields artifact formula version source formula_sha256",
            ])
            self.assertEqual(set(manifest_lines[2:]), expected_manifest_lines)
            self.assertIn(
                "@loader_path/libpng16.16.dylib",
                install_name_tool_log.read_text(encoding="utf-8"),
            )


if __name__ == "__main__":
    unittest.main()
