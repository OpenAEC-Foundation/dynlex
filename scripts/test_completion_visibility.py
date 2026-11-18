#!/usr/bin/env python3

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def completion_labels(compiler: Path, source: Path, position: str = "1:1") -> set[str]:
    return set(re.findall(r'^  - label="([^"]*)"', completion_output(compiler, source, position), re.MULTILINE))


def completion_output(compiler: Path, source: Path, position: str) -> str:
    result = subprocess.run(
        [str(compiler), str(source), "--emit-completions", position],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout


def check_shared_parent_completion(compiler: Path) -> None:
    source = '''fixed integer means: @intrinsic("fix", @intrinsic("type", "int", 32))
type means: @intrinsic("type", "type")
nothing means: @intrinsic("type", "nothing")
function set {arguments:variables} to value:
    replacement:
        @intrinsic("store", @intrinsic("extract element", variables, 0), value)
{fixed integer:count} {type:kind} array means: @intrinsic("array", count, kind)
function add value to target:
    replacement:
        @intrinsic("discard", value)
        @intrinsic("discard", target)
flex function {nothing:left}[ and|.] {nothing:right}:
    replacement:
        @intrinsic("discard", left)
        @intrinsic("discard", right)
'''
    prefix = "set {same line value} to 1 and add 2 to "
    name = "same line value"
    with tempfile.TemporaryDirectory(prefix="dynlex-completion-parent-") as directory:
        path = Path(directory) / "main.dl"
        path.write_text(source + prefix + name + "\n", encoding="utf-8")
        position = f"{len(source.splitlines()) + 1}:{len(prefix) + 1}"
        assert name in completion_labels(compiler, path, position), "completion lost the earlier variable"


def main() -> int:
    project_dir = Path(__file__).resolve().parent.parent
    compiler = Path(sys.argv[1]) if len(sys.argv) > 1 else project_dir / "build" / "dynlex"
    check_shared_parent_completion(compiler)
    fixture_dir = project_dir / "tests" / "integration" / "local_functions"

    imported_labels = completion_labels(compiler, fixture_dir / "main.dl")
    if "secret" in imported_labels:
        raise AssertionError("completion exposed a local function from an imported source file")

    declaring_labels = completion_labels(compiler, fixture_dir / "first.dl")
    if "secret" not in declaring_labels:
        raise AssertionError("completion hid a local function from its declaring source file")

    for source, position in [
        (fixture_dir / "main.dl", "6:12"),
        (fixture_dir / "first.dl", "31:12"),
    ]:
        labels = completion_labels(compiler, source, position)
        placeholders = sorted(
            label for label in labels if label.startswith("<") and label.endswith(">")
        )
        if placeholders:
            raise AssertionError(
                f"completion invented parameter names in {source.name}: {placeholders}"
            )

    multi_word_source = project_dir / "tests" / "integration" / "multi_word_variables" / "main.dl"
    before_declaration = completion_labels(compiler, multi_word_source, "20:12")
    for future_name in ["same line value", "val", "numeric value"]:
        if future_name in before_declaration:
            raise AssertionError(f"completion exposed '{future_name}' before its declaration")
    if "half width" not in before_declaration:
        raise AssertionError("completion hid a multi-word variable declared on an earlier line")

    after_same_line_declaration = completion_labels(compiler, multi_word_source, "22:41")
    if "same line value" not in after_same_line_declaration:
        raise AssertionError("completion hid a multi-word variable declared earlier on the cursor line")

    partial_multi_word = completion_output(compiler, multi_word_source, "20:18")
    if not re.search(
        r'^  - label="half width".* edit=\(20:12-20:18 -> half width\)$',
        partial_multi_word,
        re.MULTILINE,
    ):
        raise AssertionError("completion did not replace a partially entered multi-word variable")

    trailing_space = completion_output(compiler, multi_word_source, "20:17")
    if not re.search(
        r'^  - label="half width".* edit=\(20:12-20:17 -> half width\)$',
        trailing_space,
        re.MULTILINE,
    ):
        raise AssertionError("completion duplicated a multi-word variable prefix before the cursor")

    shorthand_source = (
        project_dir / "tests" / "integration" / "declaration_shorthand_source_mapping" / "main.dl"
    )
    shorthand_body_labels = completion_labels(compiler, shorthand_source, "4:30")
    if "value" not in shorthand_body_labels:
        raise AssertionError("completion hid an inline shorthand parameter from its transformed body")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
