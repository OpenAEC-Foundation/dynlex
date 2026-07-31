#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = PROJECT_DIR / "scripts"
PINNED_REPOSITORY = "https://github.com/OpenAEC-Foundation/llvm-project.git"
PINNED_REVISION = "102332db2c124acd59d44b3463d12d9c2da218a7"
PINNED_SCHEMA = "2"


class LlvmToolchainTests(unittest.TestCase):
    def test_windows_checkout_keeps_shell_inputs_lf_terminated(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-lf-checkout-") as temporary_directory:
            checkout_root = Path(temporary_directory)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(PROJECT_DIR),
                    "-c",
                    "core.autocrlf=true",
                    "checkout-index",
                    "--force",
                    f"--prefix={checkout_root}/",
                    "--",
                    "metadata/LLVM_TOOLCHAIN",
                    "metadata/LLVM_MINGW_TOOLCHAIN",
                    "metadata/VCPKG_TOOLCHAIN",
                    "metadata/release-manifest.txt",
                    "scripts/llvm_toolchain.sh",
                    "scripts/release-asset.sh",
                    "web/install.sh",
                ],
                check=True,
                capture_output=True,
            )

            for relative_path in (
                "metadata/LLVM_TOOLCHAIN",
                "metadata/LLVM_MINGW_TOOLCHAIN",
                "metadata/VCPKG_TOOLCHAIN",
                "metadata/release-manifest.txt",
                "scripts/llvm_toolchain.sh",
                "scripts/release-asset.sh",
                "web/install.sh",
            ):
                contents = (checkout_root / relative_path).read_bytes()
                self.assertNotIn(b"\r\n", contents, relative_path)

    def test_linked_worktrees_share_the_primary_toolchain_cache(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-llvm-worktree-") as temporary_directory:
            temporary_root = Path(temporary_directory)
            primary = temporary_root / "primary"
            linked = temporary_root / "linked"
            (primary / "scripts").mkdir(parents=True)
            (primary / "metadata").mkdir()
            (primary / "scripts" / "llvm_toolchain.sh").write_text(
                (SCRIPTS_DIR / "llvm_toolchain.sh").read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            (primary / "metadata" / "LLVM_TOOLCHAIN").write_text(
                (PROJECT_DIR / "metadata" / "LLVM_TOOLCHAIN").read_text(encoding="utf-8"),
                encoding="utf-8",
            )

            subprocess.run(["git", "init", str(primary)], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(primary), "add", "."], check=True, capture_output=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(primary),
                    "-c",
                    "user.name=DynLex Tests",
                    "-c",
                    "user.email=tests@dynlex.invalid",
                    "commit",
                    "-m",
                    "fixture",
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["git", "-C", str(primary), "worktree", "add", "--detach", str(linked), "HEAD"],
                check=True,
                capture_output=True,
            )

            environment = os.environ.copy()
            environment.pop("DYNLEX_LLVM_CACHE_DIR", None)
            resolved_caches = []
            for checkout in (primary, linked):
                completed = subprocess.run(
                    [
                        "bash",
                        "-c",
                        '. "$1"; printf "%s\\n" "$DYNLEX_LLVM_CACHE_DIR"',
                        "bash",
                        str(checkout / "scripts" / "llvm_toolchain.sh"),
                    ],
                    capture_output=True,
                    env=environment,
                    text=True,
                )
                self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
                resolved_caches.append(Path(completed.stdout.strip()))

            expected_cache = primary / ".cache" / "llvm-toolchain"
            self.assertEqual(resolved_caches, [expected_cache, expected_cache])

    def test_toolchain_pin_and_cache_layout_have_one_source_of_truth(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-llvm-contract-") as temporary_directory:
            environment = os.environ.copy()
            environment["DYNLEX_LLVM_CACHE_DIR"] = temporary_directory
            completed = subprocess.run(
                [
                    "bash",
                    "-c",
                    """
set -euo pipefail
. "$1"
printf '%s\\n' \
    "$DYNLEX_LLVM_REPOSITORY" \
    "$DYNLEX_LLVM_REVISION" \
    "$DYNLEX_LLVM_MAJOR_VERSION" \
    "$DYNLEX_LLVM_SOURCE_DIR" \
    "$DYNLEX_LLVM_NATIVE_INSTALL_DIR" \
    "$DYNLEX_LLVM_WEB_INSTALL_DIR"
""",
                    "bash",
                    str(SCRIPTS_DIR / "llvm_toolchain.sh"),
                ],
                capture_output=True,
                env=environment,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            cache_directory = Path(temporary_directory)
            self.assertEqual(
                completed.stdout.splitlines(),
                [
                    PINNED_REPOSITORY,
                    PINNED_REVISION,
                    "23",
                    str(cache_directory / "source"),
                    str(cache_directory / "native" / "install"),
                    str(cache_directory / "web" / "install"),
                ],
            )

    def test_build_entrypoints_always_use_the_pinned_toolchain(self) -> None:
        native_build = (SCRIPTS_DIR / "build.sh").read_text(encoding="utf-8")
        web_build = (SCRIPTS_DIR / "build_web.sh").read_text(encoding="utf-8")

        self.assertIn('. "$SCRIPT_DIR/llvm_toolchain.sh"', native_build)
        self.assertIn("dynlex_ensure_llvm_toolchain native", native_build)
        self.assertIn('. "$SCRIPT_DIR/llvm_toolchain.sh"', web_build)
        self.assertIn("dynlex_ensure_llvm_toolchain web", web_build)

        for contents in (native_build, web_build):
            self.assertNotIn("llvm_version.sh", contents)
            self.assertNotIn("${LLVM_DIR", contents)
            self.assertNotIn("${DYNLEX_LLVM_VERSION", contents)

        toolchain = (SCRIPTS_DIR / "llvm_toolchain.sh").read_text(encoding="utf-8")
        self.assertIn("llvm-tblgen;llvm-dwarfdump", toolchain)
        self.assertIn("sparse-checkout set llvm cmake libc third-party", toolchain)
        self.assertIn("for source_component in llvm cmake libc third-party", toolchain)
        self.assertNotIn("mapfile", toolchain)
        self.assertNotIn("native_arguments", toolchain)
        cmake = (PROJECT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("add_definitions(${LLVM_DEFINITIONS})", cmake)
        self.assertRegex(
            cmake,
            r"separate_arguments\(DYNLEX_LLVM_COMPILE_OPTIONS NATIVE_COMMAND \"\$\{LLVM_DEFINITIONS\}\"\)",
        )
        self.assertRegex(
            cmake,
            r"function\(dynlex_link_llvm target\)[\s\S]*target_compile_options\(\$\{target\} PRIVATE \$\{DYNLEX_LLVM_COMPILE_OPTIONS\}\)",
        )
        debug_test = (SCRIPTS_DIR / "test_debug_info.py").read_text(encoding="utf-8")
        self.assertIn('SCRIPTS_DIR / "llvm_toolchain.sh"', debug_test)
        self.assertIn('"$DYNLEX_LLVM_NATIVE_INSTALL_DIR"', debug_test)
        self.assertNotIn('repo_root / ".cache" / "llvm-toolchain"', debug_test)
        self.assertNotIn("shutil.which", debug_test)

    def test_windows_native_llvm_uses_and_records_the_static_crt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="dynlex-llvm-windows-crt-") as temporary_directory:
            environment = os.environ.copy()
            environment["DYNLEX_LLVM_CACHE_DIR"] = temporary_directory
            completed = subprocess.run(
                [
                    "bash",
                    "-c",
                    """
set -euo pipefail
uname() {
    if [ "$1" = -s ]; then
        printf '%s\n' MINGW64_NT
    else
        command uname "$@"
    fi
}
. "$1"
dynlex_llvm_marker_contents native
dynlex_llvm_marker_contents web
dynlex_native_llvm_cmake_arguments
""",
                    "bash",
                    str(SCRIPTS_DIR / "llvm_toolchain.sh"),
                ],
                capture_output=True,
                env=environment,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertEqual(
                completed.stdout.splitlines(),
                [
                    f"{PINNED_REVISION}:{PINNED_SCHEMA}:native:static-msvc-crt",
                    f"{PINNED_REVISION}:{PINNED_SCHEMA}:web",
                    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
                ],
            )
    def test_normal_native_build_is_optimized_without_disabling_invariants(self) -> None:
        native_build = (SCRIPTS_DIR / "build.sh").read_text(encoding="utf-8")
        cmake = (PROJECT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("BUILD_TYPE=Optimized", native_build)
        self.assertIn("--debug) BUILD_TYPE=Debug", native_build)
        self.assertIn('CMAKE_CXX_FLAGS_OPTIMIZED', cmake)
        self.assertIn("-O2 -g", cmake)
        self.assertNotRegex(cmake, r'CMAKE_CXX_FLAGS_OPTIMIZED[^\n]*NDEBUG')

    def test_codegen_uses_the_llvm_23_apis(self) -> None:
        codegen_directory = PROJECT_DIR / "src" / "cpp" / "compiler" / "codegen"
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(codegen_directory.glob("*"))
            if path.suffix in {".cpp", ".h", ".inl"}
        )

        self.assertIn("->hasTerminator()", source)
        self.assertNotIn("basicBlockHasTerminator", source)
        self.assertNotRegex(source, r"!\s*[^;\n]*->getTerminator\(\)")
        self.assertIn("llvm::UncondBrInst", source)
        self.assertNotIn("llvm::BranchInst", source)
        self.assertIn("llvm::Triple targetTriple", source)
        self.assertIn("llvm::ArrayRef<llvm::Metadata *>{}", source)

    def test_cmake_requires_the_pinned_llvm_major(self) -> None:
        cmake = (PROJECT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('set(DYNLEX_LLVM_METADATA_FILE "${CMAKE_SOURCE_DIR}/metadata/LLVM_TOOLCHAIN")', cmake)
        self.assertIn('set(DYNLEX_REQUIRED_LLVM_VERSION "${DYNLEX_LLVM_MAJOR}")', cmake)
        self.assertIn(
            'if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL "${DYNLEX_REQUIRED_LLVM_VERSION}.0.0git")',
            cmake,
        )
        self.assertIn("include/llvm/Support/VCSRevision.h", cmake)
        self.assertIn("${DYNLEX_LLVM_REPOSITORY}", cmake)
        self.assertIn("${DYNLEX_LLVM_REQUIRED_PROFILE}", cmake)
        self.assertIn("CPACK_DEBIAN_PACKAGE_DEPENDS", cmake)
        for package in (
            "binutils",
            "build-essential",
            "clang",
            "libc6-dev",
            "libfreetype-dev",
            "libgl-dev",
            "libglfw3-dev",
        ):
            self.assertIn(package, cmake)
        self.assertNotIn("zlib1.dll", cmake)

    def test_launchpad_package_bundles_and_builds_the_pinned_source(self) -> None:
        control = (PROJECT_DIR / "packaging/launchpad/debian/control").read_text(encoding="utf-8")
        rules = (PROJECT_DIR / "packaging/launchpad/debian/rules").read_text(encoding="utf-8")
        source_builder = (
            PROJECT_DIR / "packaging/launchpad/scripts/build-source-package.sh"
        ).read_text(encoding="utf-8")

        self.assertNotIn("llvm-20-dev", control)
        self.assertNotIn("DYNLEX_LLVM_VERSION", rules)
        self.assertIn("./scripts/build_llvm.sh native", rules)
        self.assertIn('"$DYNLEX_LLVM_SOURCE_DIR"', source_builder)
        self.assertIn(".cache/llvm-toolchain/source", source_builder)
        self.assertIn(".dynlex-llvm-source", source_builder)
        self.assertIn("--exclude=.git", source_builder)
        self.assertNotIn("--exclude=/libc", source_builder)
        self.assertNotIn("--exclude=/third-party", source_builder)


if __name__ == "__main__":
    unittest.main()
