#!/usr/bin/env python3
import argparse
import pathlib
import re
import subprocess
import sys


TAG_RE = re.compile(r"<(/?)([^>]+)>")


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan DynLex semantic token output and report untokenized source spans."
    )
    parser.add_argument("paths", nargs="*", help="Optional .dl files or directories. Defaults to all repo .dl files.")
    parser.add_argument("--show-tagged", action="store_true", help="Print the tagged token output before findings.")
    parser.add_argument("--quiet-errors", action="store_true", help="Suppress files that fail before token emission.")
    return parser.parse_args()


def discover_files(root: pathlib.Path, paths: list[str]) -> list[pathlib.Path]:
    if not paths:
        return sorted(root.glob("**/*.dl"))

    discovered: list[pathlib.Path] = []
    for raw_path in paths:
        path = (root / raw_path).resolve() if not pathlib.Path(raw_path).is_absolute() else pathlib.Path(raw_path)
        if path.is_dir():
            discovered.extend(sorted(path.glob("**/*.dl")))
        elif path.suffix == ".dl":
            discovered.append(path)
    return sorted(dict.fromkeys(discovered))


def emit_tokens(root: pathlib.Path, file_path: pathlib.Path) -> tuple[bool, str]:
    relative_path = str(file_path.relative_to(root))
    try:
        proc = subprocess.run(
            ["./build/dynlex", relative_path, "--emit-tokens"],
            cwd=root,
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        return False, f"failed to start ./build/dynlex: {exc}"
    output = proc.stdout if proc.stdout else proc.stderr
    return proc.returncode == 0, output


def find_plain_segments(line: str) -> list[str]:
    stack: list[str] = []
    pos = 0
    plain: list[str] = []

    for match in TAG_RE.finditer(line):
        if match.start() > pos:
            segment = line[pos:match.start()]
            if not stack and segment.strip():
                plain.append(segment)
        if match.group(1):
            if stack:
                stack.pop()
        else:
            stack.append(match.group(2))
        pos = match.end()

    if pos < len(line):
        segment = line[pos:]
        if not stack and segment.strip():
            plain.append(segment)

    return plain


def main() -> int:
    args = parse_args()
    root = repo_root()
    files = discover_files(root, args.paths)

    if not files:
        print("No .dl files found.", file=sys.stderr)
        return 1

    findings = 0
    for file_path in files:
        ok, output = emit_tokens(root, file_path)
        rel_path = file_path.relative_to(root)

        if args.show_tagged:
            print(f"== {rel_path} ==")
            print(output.rstrip())
            print()

        if not ok:
            if not args.quiet_errors:
                print(f"{rel_path}: token emission failed")
                for line in output.splitlines()[:5]:
                    print(f"  {line}")
                print()
            continue

        file_findings = []
        for line_number, line in enumerate(output.splitlines(), 1):
            segments = find_plain_segments(line)
            if segments:
                file_findings.append((line_number, segments))

        if not file_findings:
            continue

        findings += len(file_findings)
        print(rel_path)
        for line_number, segments in file_findings:
            joined = " | ".join(repr(segment) for segment in segments)
            print(f"  {line_number}: {joined}")
        print()

    if findings == 0:
        print("No untokenized non-whitespace source spans found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
