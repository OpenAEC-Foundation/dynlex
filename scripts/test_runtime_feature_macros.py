#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
RUNTIME_DIR = PROJECT_DIR / "src" / "runtime"


class RuntimeFeatureMacroTests(unittest.TestCase):
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
