#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent


def write_executable(path: Path, contents: str) -> None:
    path.write_text(contents, encoding="utf-8")
    path.chmod(0o755)


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

            write_executable(
                llvm_prefix / "bin" / "llvm-config",
                "#!/bin/bash\nprintf '20.1.8\\n'\n",
            )
            write_executable(
                bin_directory / "uname",
                "#!/bin/bash\nprintf 'Darwin\\n'\n",
            )
            write_executable(
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
                write_executable(bin_directory / command, "#!/bin/bash\nexit 0\n")
            write_executable(
                bin_directory / "cut",
                "#!/bin/bash\nIFS=. read -r first remainder\nprintf '%s\\n' \"$first\"\n",
            )
            write_executable(
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



class WebDependencyInstallerTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "web installer requires a POSIX host")
    def test_web_build_activates_configured_emsdk_over_wrong_path_version(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-web-build-") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "project"
            scripts = project / "scripts"
            wrong_bin = root / "wrong-bin"
            configured_bin = root / "emsdk" / "upstream" / "emscripten"
            llvm_root = root / "llvm-wasm-20"
            scripts.mkdir(parents=True)
            wrong_bin.mkdir()
            configured_bin.mkdir(parents=True)
            (project / "src" / "web" / "ide" / "public" / "compiler").mkdir(parents=True)
            (llvm_root / "install" / "lib" / "cmake" / "llvm").mkdir(parents=True)
            (llvm_root / "install" / "lib" / "cmake" / "llvm" / "LLVMConfig.cmake").write_text(
                "mock LLVM config\n", encoding="utf-8"
            )
            shutil.copy2(SCRIPT_DIR / "build_web.sh", scripts / "build_web.sh")
            shutil.copy2(SCRIPT_DIR / "llvm_version.sh", scripts / "llvm_version.sh")

            log = root / "commands.log"
            write_executable(
                wrong_bin / "emcc",
                "#!/bin/bash\nprintf 'wrong emcc %s\\n' \"$*\" >> \"$MOCK_LOG\"\nprintf 'emcc (mock) 1.0.0 wrong\\n'\n",
            )
            write_executable(
                wrong_bin / "emcmake",
                "#!/bin/bash\nprintf 'wrong emcmake %s\\n' \"$*\" >> \"$MOCK_LOG\"\nexit 1\n",
            )
            write_executable(
                configured_bin / "emcc",
                "#!/bin/bash\nprintf 'configured emcc %s\\n' \"$*\" >> \"$MOCK_LOG\"\nprintf 'emcc (mock) 6.0.3 configured\\n'\n",
            )
            write_executable(
                configured_bin / "emcmake",
                "#!/bin/bash\nprintf 'configured emcmake %s\\n' \"$*\" >> \"$MOCK_LOG\"\n\"$@\"\n",
            )
            write_executable(
                root / "emsdk" / "emsdk_env.sh",
                f"#!/bin/bash\nexport PATH=\"{configured_bin}:$PATH\"\n",
            )
            write_executable(
                wrong_bin / "cmake",
                """#!/bin/bash
printf 'cmake %s\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == "--build" ]]; then
    mkdir -p "$2"
    printf 'js\n' > "$2/dynlex_web.js"
    printf 'wasm\n' > "$2/dynlex_web.wasm"
fi
""",
            )
            write_executable(wrong_bin / "ninja", "#!/bin/bash\nexit 0\n")

            environment = os.environ.copy()
            environment.update({
                "DYNLEX_EMSDK_ROOT": str(root / "emsdk"),
                "DYNLEX_LLVM_WASM_ROOT": str(llvm_root),
                "HOME": str(root),
                "MOCK_LOG": str(log),
                "PATH": f"{wrong_bin}:/usr/bin:/bin",
            })
            bash = shutil.which("bash")
            self.assertIsNotNone(bash)
            completed = subprocess.run(
                [bash, str(scripts / "build_web.sh")],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            commands = log.read_text(encoding="utf-8").splitlines()
            self.assertIn("configured emcc --version", commands)
            self.assertTrue(any(command.startswith("configured emcmake cmake ") for command in commands))
            self.assertFalse(any(command.startswith("wrong em") for command in commands))

    @unittest.skipIf(os.name == "nt", "web installer requires a POSIX host")
    def test_web_mode_installs_versioned_emscripten_and_wasm_llvm(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-web-install-") as temporary_directory:
            root = Path(temporary_directory)
            bin_directory = root / "bin"
            emsdk_root = root / "emsdk"
            llvm_root = root / "toolchains" / "llvm-wasm-20"
            native_llvm_bin = root / "native-llvm" / "bin"
            bin_directory.mkdir()
            (emsdk_root / "upstream" / "emscripten").mkdir(parents=True)
            native_llvm_bin.mkdir(parents=True)

            log = root / "commands.log"
            write_executable(
                emsdk_root / "emsdk",
                "#!/bin/bash\nprintf 'emsdk %s\\n' \"$*\" >> \"$MOCK_LOG\"\n",
            )
            write_executable(
                emsdk_root / "emsdk_env.sh",
                f"#!/bin/bash\nexport EMSDK=\"$DYNLEX_EMSDK_ROOT\"\nexport PATH=\"{emsdk_root / 'upstream' / 'emscripten'}:$PATH\"\n",
            )
            write_executable(
                emsdk_root / "upstream" / "emscripten" / "emcc",
                "#!/bin/bash\nprintf 'emcc %s\\n' \"$*\" >> \"$MOCK_LOG\"\n",
            )
            write_executable(
                emsdk_root / "upstream" / "emscripten" / "emcmake",
                "#!/bin/bash\nprintf 'emcmake %s\\n' \"$*\" >> \"$MOCK_LOG\"\n\"$@\"\n",
            )
            write_executable(native_llvm_bin / "llvm-tblgen", "#!/bin/bash\nexit 0\n")
            write_executable(
                bin_directory / "llvm-config-20",
                f"#!/bin/bash\ncase \"$1\" in\n  --bindir) printf '%s\\n' '{native_llvm_bin}' ;;\n  --version) printf '20.1.8\\n' ;;\nesac\n",
            )
            write_executable(
                bin_directory / "git",
                """#!/bin/bash
printf 'git %s\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == clone ]]; then
    destination="${@: -1}"
    mkdir -p "$destination/.git" "$destination/llvm"
elif [[ "$1" == -C && "$3" == describe ]]; then
    printf 'llvmorg-20.1.8\n'
fi
""",
            )
            write_executable(
                bin_directory / "cmake",
                """#!/bin/bash
printf 'cmake %s\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == --build ]]; then
    mkdir -p "$MOCK_LLVM_ROOT/install/lib/cmake/llvm"
    printf 'mock LLVM config\n' > "$MOCK_LLVM_ROOT/install/lib/cmake/llvm/LLVMConfig.cmake"
fi
""",
            )

            environment = os.environ.copy()
            environment.update({
                "DYNLEX_EMSDK_ROOT": str(emsdk_root),
                "DYNLEX_LLVM_WASM_ROOT": str(llvm_root),
                "HOME": str(root),
                "MOCK_LLVM_ROOT": str(llvm_root),
                "MOCK_LOG": str(log),
                "PATH": f"{bin_directory}:/usr/bin:/bin",
            })

            bash = shutil.which("bash")
            self.assertIsNotNone(bash)
            completed = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh"), "--web"],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

            commands = log.read_text(encoding="utf-8").splitlines()
            self.assertIn("emsdk install 6.0.3", commands)
            self.assertIn("emsdk activate 6.0.3", commands)
            clone = next(command for command in commands if command.startswith("git clone "))
            self.assertIn("--branch llvmorg-20.1.8", clone)
            configure = next(command for command in commands if command.startswith("emcmake cmake "))
            self.assertIn("-DLLVM_TARGETS_TO_BUILD=WebAssembly", configure)
            self.assertIn(f"-DLLVM_NATIVE_TOOL_DIR={native_llvm_bin}", configure)
            self.assertIn(f"-DLLVM_TABLEGEN={native_llvm_bin / 'llvm-tblgen'}", configure)
            self.assertIn(
                f"cmake --build {llvm_root / 'build'} --target install --parallel 2",
                commands,
            )
            self.assertEqual(
                (llvm_root / ".dynlex-version").read_text(encoding="utf-8"),
                "LLVM 20.1.8\nEmscripten 6.0.3\n",
            )

            log.write_text("", encoding="utf-8")
            repeated = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh"), "--web"],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(repeated.returncode, 0, repeated.stdout + repeated.stderr)
            repeated_commands = log.read_text(encoding="utf-8").splitlines()
            self.assertFalse(any(command.startswith("git ") for command in repeated_commands))
            self.assertFalse(any(command.startswith("cmake ") for command in repeated_commands))
            self.assertFalse(any(command.startswith("emcmake ") for command in repeated_commands))

            log.write_text("", encoding="utf-8")
            environment["DYNLEX_EMSCRIPTEN_VERSION"] = "6.0.4"
            migrated = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh"), "--web"],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(migrated.returncode, 0, migrated.stdout + migrated.stderr)
            migrated_commands = log.read_text(encoding="utf-8").splitlines()
            self.assertFalse(any(command.startswith("git clone ") for command in migrated_commands))
            self.assertTrue(any(command.startswith("emcmake ") for command in migrated_commands))
            self.assertTrue(any(command.startswith("cmake --build ") for command in migrated_commands))
            self.assertEqual(
                (llvm_root / ".dynlex-version").read_text(encoding="utf-8"),
                "LLVM 20.1.8\nEmscripten 6.0.4\n",
            )

            log.write_text("", encoding="utf-8")
            (llvm_root / ".dynlex-version").unlink()
            stale_marker = llvm_root / "build" / "stale-cache"
            stale_marker.parent.mkdir(parents=True, exist_ok=True)
            stale_marker.write_text("unknown toolchain", encoding="utf-8")
            unversioned = subprocess.run(
                [bash, str(SCRIPT_DIR / "install.sh"), "--web"],
                capture_output=True,
                env=environment,
                text=True,
                timeout=10,
            )
            self.assertEqual(unversioned.returncode, 0, unversioned.stdout + unversioned.stderr)
            unversioned_commands = log.read_text(encoding="utf-8").splitlines()
            self.assertTrue(any(command.startswith("git clone ") for command in unversioned_commands))
            self.assertTrue(any(command.startswith("emcmake ") for command in unversioned_commands))
            self.assertFalse(stale_marker.exists())
            self.assertEqual(
                (llvm_root / ".dynlex-version").read_text(encoding="utf-8"),
                "LLVM 20.1.8\nEmscripten 6.0.4\n",
            )


if __name__ == "__main__":
    unittest.main()
