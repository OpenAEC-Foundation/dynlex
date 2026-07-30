#!/usr/bin/env python3

import pathlib
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_spirv_main_return.py <compiler>", file=sys.stderr)
        return 2

    compiler = pathlib.Path(sys.argv[1]).resolve()
    project = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="dynlex-spirv-return-") as temporary_directory:
        temporary = pathlib.Path(temporary_directory)
        source = temporary / "main.dl"
        output = temporary / "main.spv"
        source.write_text("import lib/std.dl\n\nreturn 0\n", encoding="utf-8")
        result = subprocess.run(
            [str(compiler), str(source), "--emit-spirv", "--shader-stage=fragment", "-o", str(output)],
            cwd=project,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            return result.returncode
        if not output.is_file() or output.stat().st_size == 0:
            print("SPIR-V compilation with an explicit main return produced no output", file=sys.stderr)
            return 1

        invalid_source = temporary / "invalid_main.dl"
        invalid_output = temporary / "invalid_main.spv"
        invalid_source.write_text(
            "function return value:\n"
            "    replacement:\n"
            "        @intrinsic(\"return\", value)\n\n"
            "flex function [an|] integer:\n"
            "    replacement:\n"
            "        @intrinsic(\"type\", \"int\")\n\n"
            "class:\n"
            "    patterns:\n"
            "        [a|] box\n"
            "    members:\n"
            "        value as an integer\n\n"
            "function a box containing {integer:value}:\n"
            "    replacement:\n"
            "        @intrinsic(\"construct\", a box, value)\n\n"
            "return a box containing 1\n",
            encoding="utf-8",
        )
        invalid_result = subprocess.run(
            [
                str(compiler),
                str(invalid_source),
                "--emit-spirv",
                "--shader-stage=fragment",
                "-o",
                str(invalid_output),
            ],
            cwd=project,
            capture_output=True,
            text=True,
            check=False,
        )
        expected_error = "Program return value must be convertible to a 32-bit integer, got a box"
        if invalid_result.returncode == 0 or expected_error not in invalid_result.stderr:
            sys.stderr.write(invalid_result.stdout)
            sys.stderr.write(invalid_result.stderr)
            print("SPIR-V compilation accepted an invalid program return value", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
