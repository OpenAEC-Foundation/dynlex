#!/usr/bin/env python3
"""Keep the short compiler gate independent of the application libraries."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1] / "tests" / "required"


def main() -> int:
    failures = []
    fixtures = sorted(path for path in ROOT.iterdir() if path.is_dir())
    for fixture in fixtures:
        if not (fixture / "main.dl").is_file():
            failures.append(f"{fixture}: missing main.dl")
        if (fixture / "compile_timeout_seconds.txt").exists():
            failures.append(f"{fixture}: focused tests use the default compile budget")
        for source in fixture.rglob("*.dl"):
            for number, line in enumerate(source.read_text().splitlines(), 1):
                stripped = line.strip()
                if not stripped.startswith("import "):
                    continue
                target = stripped[7:].split("#", 1)[0].strip()
                local_target = (source.parent / target).is_file() or (fixture / target).is_file()
                if "/" in target or "\\" in target or not local_target:
                    failures.append(f"{source}:{number}: required imports must use a local helper file")
    if not fixtures:
        failures.append(f"{ROOT}: no required fixtures")
    for failure in failures:
        print(failure, file=sys.stderr)
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
