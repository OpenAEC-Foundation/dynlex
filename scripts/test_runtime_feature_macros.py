#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
RUNTIME_DIR = PROJECT_DIR / "src" / "runtime"


class RuntimeFeatureMacroTests(unittest.TestCase):
    def test_stat_timestamp_accessors_compile_for_linux_and_macos_layouts(self) -> None:
        bootstrap_major = next(
            line.split()[1]
            for line in (PROJECT_DIR / "metadata" / "LLVM_TOOLCHAIN").read_text(
                encoding="utf-8"
            ).splitlines()
            if line.startswith("bootstrap ")
        )
        compiler = (
            os.environ["DYNLEX_LLVM_BOOTSTRAP_CC"]
            if os.name == "nt"
            else f"clang-{bootstrap_major}"
        )

        with tempfile.TemporaryDirectory(prefix="dynlex-stat-layout-") as temporary_directory:
            temporary_root = Path(temporary_directory)
            fake_include = temporary_root / "include"
            (fake_include / "sys").mkdir(parents=True)
            (fake_include / "stdint.h").write_text(
                "typedef signed int int32_t;\n"
                "typedef signed long long int64_t;\n",
                encoding="utf-8",
            )
            (fake_include / "sys" / "stat.h").write_text(
                "struct dynlex_test_timestamp { long tv_sec; long tv_nsec; };\n"
                "struct stat {\n"
                "#if defined(__APPLE__)\n"
                "  struct dynlex_test_timestamp st_atimespec;\n"
                "  struct dynlex_test_timestamp st_mtimespec;\n"
                "#else\n"
                "  struct dynlex_test_timestamp st_atim;\n"
                "  struct dynlex_test_timestamp st_mtim;\n"
                "#endif\n"
                "};\n",
                encoding="utf-8",
            )
            fixture = temporary_root / "stat_layout.c"
            fixture.write_text(
                '#include "filesystemStatPosix.h"\n'
                "int main(void) {\n"
                "  struct stat value = {0};\n"
                "  return (int)(dynlex_stat_access_seconds(&value)\n"
                "    + dynlex_stat_access_nanoseconds(&value)\n"
                "    + dynlex_stat_modification_seconds(&value)\n"
                "    + dynlex_stat_modification_nanoseconds(&value));\n"
                "}\n",
                encoding="utf-8",
            )

            for platform_macro in ("__linux__", "__APPLE__"):
                completed = subprocess.run(
                    [
                        compiler,
                        "-std=c11",
                        "-Wall",
                        "-Wextra",
                        "-Wpedantic",
                        "-Werror",
                        "-fsyntax-only",
                        "-nostdinc",
                        "-U__linux__",
                        "-U__APPLE__",
                        f"-D{platform_macro}=1",
                        "-I",
                        str(fake_include),
                        "-I",
                        str(RUNTIME_DIR),
                        str(fixture),
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    completed.returncode,
                    0,
                    f"{platform_macro}: {completed.stdout}{completed.stderr}",
                )

    def test_platform_feature_macros_precede_every_runtime_system_header(self) -> None:
        feature_header = (RUNTIME_DIR / "platformFeatureTest.h").read_text(encoding="utf-8")
        linux_macro = "#define _GNU_SOURCE"
        darwin_macro = "#define _DARWIN_C_SOURCE"
        posix_macro = "#define _POSIX_C_SOURCE 200809L"

        self.assertIn("defined(__linux__)", feature_header)
        self.assertIn("defined(DYNLEX_REQUIRE_GNU_SOURCE)", feature_header)
        self.assertIn("defined(__APPLE__)", feature_header)
        self.assertIn("!defined(_WIN32)", feature_header)
        self.assertIn(linux_macro, feature_header)
        self.assertIn(darwin_macro, feature_header)
        self.assertIn(posix_macro, feature_header)
        self.assertLess(feature_header.index(linux_macro), feature_header.index(posix_macro))
        self.assertLess(feature_header.index(darwin_macro), feature_header.index(posix_macro))

        for source_name in (
            "filesystemRuntimePosix.c",
            "filesystemTransactionPosix.c",
            "hostRuntimePosix.c",
            "runtimeError.c",
        ):
            source = (RUNTIME_DIR / source_name).read_text(encoding="utf-8")
            self.assertTrue(
                source.startswith('#include "platformFeatureTest.h"\n'),
                f"{source_name} must select platform APIs before any system header",
            )
            self.assertNotIn("#define _POSIX_C_SOURCE", source)
            self.assertNotIn("#define _GNU_SOURCE", source)
            self.assertNotIn("#define _DARWIN_C_SOURCE", source)

        process_source = (RUNTIME_DIR / "processRuntimePosix.c").read_text(encoding="utf-8")
        self.assertTrue(
            process_source.startswith(
                "#define DYNLEX_REQUIRE_GNU_SOURCE\n"
                '#include "platformFeatureTest.h"\n'
                "#undef DYNLEX_REQUIRE_GNU_SOURCE\n"
            )
        )
        self.assertNotIn("#define _POSIX_C_SOURCE", process_source)
        self.assertNotIn("#define _GNU_SOURCE", process_source)
        self.assertNotIn("#define _DARWIN_C_SOURCE", process_source)

        cmake = (PROJECT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("src/runtime/platformFeatureTest.h", cmake)


if __name__ == "__main__":
    unittest.main()
