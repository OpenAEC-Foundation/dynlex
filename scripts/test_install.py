#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent


class LinuxDependencyInstallerTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "Linux installer requires a POSIX host")
    def test_binutils_is_installed_by_every_supported_package_manager(self) -> None:
        for package_manager in ("apt-get", "dnf", "pacman", "zypper"):
            with self.subTest(package_manager=package_manager):
                self._assert_installs_binutils(package_manager)

    def _assert_installs_binutils(self, package_manager: str) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-install-linux-") as temporary_directory:
            root = Path(temporary_directory)
            bin_directory = root / "bin"
            bin_directory.mkdir()

            self._write_executable(
                bin_directory / "uname",
                "#!/bin/bash\nprintf 'Linux\\n'\n",
            )
            self._write_executable(bin_directory / package_manager, "#!/bin/bash\nexit 0\n")
            self._write_executable(
                bin_directory / "sudo",
                "#!/bin/bash\nprintf '%s\\n' \"$*\" >> \"$SUDO_LOG\"\n",
            )
            self._write_executable(
                bin_directory / "dirname",
                "#!/bin/bash\nvalue=$1\nprintf '%s\\n' \"${value%/*}\"\n",
            )

            sudo_log = root / "sudo.log"
            environment = os.environ.copy()
            environment.update({
                "PATH": str(bin_directory),
                "SUDO_LOG": str(sudo_log),
            })

            bash = shutil.which("bash")
            self.assertIsNotNone(bash)
            completed = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh")],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

            install_commands = [
                line for line in sudo_log.read_text(encoding="utf-8").splitlines()
                if " install " in f" {line} " or line.startswith("pacman ")
            ]
            self.assertEqual(len(install_commands), 1, install_commands)
            self.assertIn("binutils", install_commands[0].split())

    @staticmethod
    def _write_executable(path: Path, contents: str) -> None:
        path.write_text(contents, encoding="utf-8")
        path.chmod(0o755)


class MacOSDependencyInstallerTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "macOS installer requires a POSIX host")
    def test_existing_tools_are_not_reinstalled_or_upgraded(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-install-") as temporary_directory:
            root = Path(temporary_directory)
            bin_directory = root / "bin"
            llvm_prefix = root / "opt" / "llvm@20"
            bin_directory.mkdir()
            (llvm_prefix / "bin").mkdir(parents=True)

            self._write_executable(
                llvm_prefix / "bin" / "llvm-config",
                "#!/bin/bash\nprintf '20.1.8\\n'\n",
            )
            self._write_executable(
                bin_directory / "uname",
                "#!/bin/bash\nprintf 'Darwin\\n'\n",
            )
            self._write_executable(
                bin_directory / "brew",
                """#!/bin/bash
printf '%s\n' "$*" >> "$BREW_LOG"
case "$1" in
    update)
        ;;
    info)
        [[ "$2" == "llvm@20" ]]
        ;;
    list)
        [[ "${@: -1}" == "freetype" ]]
        ;;
    install)
        printf 'install-upgrade=%s\n' "${HOMEBREW_NO_INSTALL_UPGRADE-}" >> "$BREW_LOG"
        ;;
    --prefix)
        case "${2-}" in
            llvm@20) printf '%s\n' "$MOCK_ROOT/opt/llvm@20" ;;
            glfw) printf '%s\n' "$MOCK_ROOT/opt/glfw" ;;
            freetype) printf '%s\n' "$MOCK_ROOT/opt/freetype" ;;
            '') printf '%s\n' "$MOCK_ROOT/homebrew" ;;
            *) exit 2 ;;
        esac
        ;;
    *)
        exit 2
        ;;
esac
""",
            )

            for command in ("ccache", "cmake", "git", "go", "node"):
                self._write_executable(bin_directory / command, "#!/bin/bash\nexit 0\n")
            self._write_executable(
                bin_directory / "cut",
                "#!/bin/bash\nIFS=. read -r first remainder\nprintf '%s\\n' \"$first\"\n",
            )
            self._write_executable(
                bin_directory / "dirname",
                "#!/bin/bash\nvalue=$1\nprintf '%s\\n' \"${value%/*}\"\n",
            )

            brew_log = root / "brew.log"
            github_path = root / "github-path"
            github_env = root / "github-env"
            environment = os.environ.copy()
            environment.update({
                "BREW_LOG": str(brew_log),
                "GITHUB_ENV": str(github_env),
                "GITHUB_PATH": str(github_path),
                "HOME": str(root),
                "LIBRARY_PATH": "/existing/lib",
                "MOCK_ROOT": str(root),
                "PATH": str(bin_directory),
            })

            bash = shutil.which("bash")
            self.assertIsNotNone(bash)

            completed = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh")],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

            brew_commands = brew_log.read_text(encoding="utf-8").splitlines()
            install_commands = [line for line in brew_commands if line.startswith("install ")]
            self.assertEqual(
                install_commands,
                ["install llvm@20 nlohmann-json glfw ninja"],
            )
            self.assertIn("install-upgrade=1", brew_commands)

            self.assertEqual(github_path.read_text(encoding="utf-8"), f"{llvm_prefix}/bin\n")
            expected_library_path = (
                f"{root}/opt/glfw/lib:{root}/opt/freetype/lib:"
                f"{root}/homebrew/lib:/existing/lib"
            )
            self.assertEqual(
                github_env.read_text(encoding="utf-8").splitlines(),
                [
                    f"LLVM_DIR={llvm_prefix}/lib/cmake/llvm",
                    "DYNLEX_LLVM_VERSION=20",
                    f"LIBRARY_PATH={expected_library_path}",
                ],
            )

    @staticmethod
    def _write_executable(path: Path, contents: str) -> None:
        path.write_text(contents, encoding="utf-8")
        path.chmod(0o755)


if __name__ == "__main__":
    unittest.main()
