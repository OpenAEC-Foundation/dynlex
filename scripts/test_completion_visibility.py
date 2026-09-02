#!/usr/bin/env python3

import re
import subprocess
import sys
from pathlib import Path


def completion_labels(compiler: Path, source: Path, position: str = "1:1") -> set[str]:
    result = subprocess.run(
        [str(compiler), str(source), "--emit-completions", position],
        check=True,
        capture_output=True,
        text=True,
    )
    return set(re.findall(r'^  - label="([^"]*)"', result.stdout, re.MULTILINE))


def main() -> int:
    project_dir = Path(__file__).resolve().parent.parent
    compiler = Path(sys.argv[1]) if len(sys.argv) > 1 else project_dir / "build" / "dynlex"
    fixture_dir = project_dir / "tests" / "required" / "local_functions"

    imported_labels = completion_labels(compiler, fixture_dir / "main.dl")
    if "secret" in imported_labels:
        raise AssertionError("completion exposed a local function from an imported source file")

    declaring_labels = completion_labels(compiler, fixture_dir / "first.dl")
    if "secret" not in declaring_labels:
        raise AssertionError("completion hid a local function from its declaring source file")

    for source, position in [
        (fixture_dir / "main.dl", "7:12"),
        (fixture_dir / "first.dl", "34:12"),
    ]:
        labels = completion_labels(compiler, source, position)
        placeholders = sorted(
            label for label in labels if label.startswith("<") and label.endswith(">")
        )
        if placeholders:
            raise AssertionError(
                f"completion invented parameter names in {source.name}: {placeholders}"
            )

    multi_word_source = project_dir / "tests" / "required" / "multi_word_variables" / "main.dl"
    before_declaration = completion_labels(compiler, multi_word_source, "26:7")
    for future_name in ["same line value", "val", "numeric value"]:
        if future_name in before_declaration:
            raise AssertionError(f"completion exposed '{future_name}' before its declaration")
    if "half width" not in before_declaration:
        raise AssertionError("completion hid a multi-word variable declared on an earlier line")

    after_same_line_declaration = completion_labels(compiler, multi_word_source, "28:41")
    if "same line value" not in after_same_line_declaration:
        raise AssertionError("completion hid a multi-word variable declared earlier on the cursor line")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
