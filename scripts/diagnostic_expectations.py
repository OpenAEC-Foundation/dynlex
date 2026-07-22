#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


LINE_NUMBERED_LOCATION = re.compile(
    r"\.dl:(?P<line>[0-9]+):(?P<columns>[0-9]+(?:-[0-9]+)?)(?=$|[\s:),\]])"
)


def normalize_diagnostics(
    text: str,
    project_dir: Path,
    *,
    reject_line_numbers: bool = False,
) -> str:
    text = text.replace("\r", "")
    project_dir_text = str(project_dir).replace("\\", "/").rstrip("/")

    path_prefixes = {project_dir_text}
    match = re.match(r"^/([A-Za-z])/(.*)$", project_dir_text)
    if match:
        drive = match.group(1)
        rest = match.group(2)
        path_prefixes.add(f"{drive.upper()}:/{rest}")
        path_prefixes.add(f"{drive.lower()}:/{rest}")

    for prefix in sorted(path_prefixes, key=len, reverse=True):
        text = text.replace(prefix + "/", "")

    line_number_match = LINE_NUMBERED_LOCATION.search(text)
    if reject_line_numbers and line_number_match:
        expectation_line = text.count("\n", 0, line_number_match.start()) + 1
        raise ValueError(
            "expected diagnostics must not contain source line numbers "
            f"(expectation line {expectation_line})"
        )

    text = LINE_NUMBERED_LOCATION.sub(
        lambda location: f".dl:{location.group('columns')}",
        text,
    )
    return "\n".join(line.rstrip() for line in text.splitlines()).rstrip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("project_dir", type=Path)
    parser.add_argument("--reject-line-numbers", action="store_true")
    arguments = parser.parse_args()

    try:
        normalized = normalize_diagnostics(
            sys.stdin.read(),
            arguments.project_dir,
            reject_line_numbers=arguments.reject_line_numbers,
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1

    sys.stdout.write(normalized)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
