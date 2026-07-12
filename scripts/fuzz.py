#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import random
import shutil
import signal
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"
FUZZ_DIR = BUILD_DIR / "fuzz"
CURRENT_INPUT = FUZZ_DIR / "current_input.dl"
CURRENT_STDOUT = FUZZ_DIR / "last_stdout.txt"
CURRENT_STDERR = FUZZ_DIR / "last_stderr.txt"
CRASH_INPUT = FUZZ_DIR / "crash_input.dl"
CRASH_META = FUZZ_DIR / "crash_meta.txt"

BYTE_NOISE = [
    "\u0000",
    "\u0007",
    "\u0008",
    "\u000b",
    "\u000c",
    "\u00a0",
    "\u2000",
    "\u2007",
    "\u200b",
    "\u200d",
    "\u2028",
    "\u2029",
    "\u202e",
    "\ufeff",
    "\ufffd",
    "\u03bb",
    "\u2603",
]

ASCII_NOISE = [
    "#",
    ":",
    "\"",
    "'",
    "\\",
    "\t",
    "    ",
    "  ",
    "(",
    ")",
    "[",
    "]",
    "{",
    "}",
    ",",
]


def discover_sources(repo_root: Path) -> list[Path]:
    candidates: list[Path] = []
    for root in (repo_root / "tests" / "games", repo_root / "tests" / "required"):
        if not root.exists():
            continue
        for path in root.rglob("*.dl"):
            if path.is_file():
                candidates.append(path)
    return sorted(candidates)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", errors="surrogateescape")


def random_line(lines: list[str], rng: random.Random) -> int:
    if not lines:
        lines.append("")
    return rng.randrange(len(lines))


def mutate_insert_noise(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    token = rng.choice(ASCII_NOISE + BYTE_NOISE)
    col = rng.randrange(len(lines[idx]) + 1)
    lines[idx] = lines[idx][:col] + token + lines[idx][col:]


def mutate_delete_span(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    if not lines[idx]:
        return
    start = rng.randrange(len(lines[idx]))
    end = rng.randrange(start + 1, min(len(lines[idx]), start + 8) + 1)
    lines[idx] = lines[idx][:start] + lines[idx][end:]


def mutate_duplicate_line(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    lines.insert(idx, lines[idx])


def mutate_remove_line(lines: list[str], rng: random.Random) -> None:
    if not lines:
        return
    del lines[random_line(lines, rng)]
    if not lines:
        lines.append("")


def mutate_shuffle_line(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    text = lines[idx]
    if len(text) < 2:
        return
    chars = list(text)
    rng.shuffle(chars)
    lines[idx] = "".join(chars)


def mutate_indent(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    stripped = lines[idx].lstrip(" \t")
    if not stripped:
        lines[idx] = rng.choice(["", "\t", "   ", " \t "])
        return
    indent = rng.choice(["", " ", "  ", "   ", "\t", "\t ", " \t", "\u00a0", "\u200b"])
    lines[idx] = indent + stripped


def mutate_join_lines(lines: list[str], rng: random.Random) -> None:
    if len(lines) < 2:
        return
    idx = rng.randrange(len(lines) - 1)
    glue = rng.choice([" ", "", "  ", "\t", "\u200b"])
    lines[idx] = lines[idx] + glue + lines[idx + 1]
    del lines[idx + 1]


def mutate_split_line(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    text = lines[idx]
    if len(text) < 2:
        return
    cut = rng.randrange(1, len(text))
    lines[idx:idx + 1] = [text[:cut], text[cut:]]


def mutate_replace_token(lines: list[str], rng: random.Random) -> None:
    idx = random_line(lines, rng)
    replacements = [
        "import",
        "function",
        "flex",
        "@intrinsic",
        "loop",
        "if",
        "return",
        "globals:",
        "patterns:",
        "members:",
        "😀",
        "\u202e",
    ]
    token = rng.choice(replacements)
    if not lines[idx]:
        lines[idx] = token
        return
    start = rng.randrange(len(lines[idx]))
    end = rng.randrange(start, min(len(lines[idx]), start + 10) + 1)
    lines[idx] = lines[idx][:start] + token + lines[idx][end:]


MUTATIONS = [
    mutate_insert_noise,
    mutate_delete_span,
    mutate_duplicate_line,
    mutate_remove_line,
    mutate_shuffle_line,
    mutate_indent,
    mutate_join_lines,
    mutate_split_line,
    mutate_replace_token,
]


def mutate_source(text: str, rng: random.Random, mutation_count: int) -> str:
    lines = text.splitlines()
    if text.endswith("\n"):
        had_trailing_newline = True
    else:
        had_trailing_newline = False

    for _ in range(max(1, mutation_count)):
        rng.choice(MUTATIONS)(lines, rng)

    result = "\n".join(lines)
    if had_trailing_newline and rng.random() < 0.7:
        result += "\n"
    if rng.random() < 0.1:
        result = "\ufeff" + result
    return result


def signal_name(returncode: int) -> str:
    if returncode >= 0:
        return ""
    signum = -returncode
    try:
        return signal.Signals(signum).name
    except ValueError:
        return f"SIG{signum}"


def compile_case(compiler: Path, input_path: Path, output_path: Path, timeout_seconds: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(compiler), str(input_path), "-o", str(output_path)],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mutate copied DynLex sources until the compiler crashes.")
    parser.add_argument("--iterations", type=int, default=1000, help="Maximum number of fuzz iterations to run.")
    parser.add_argument("--seed", type=int, default=None, help="Random seed for reproducible runs.")
    parser.add_argument(
        "--mutations",
        type=int,
        default=8,
        help="Base number of random mutations per iteration. The script adds a small random offset.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Per-compilation timeout in seconds. Timeouts are treated as crashes worth inspection.",
    )
    parser.add_argument(
        "--compiler",
        type=Path,
        default=BUILD_DIR / "dynlex",
        help="Compiler binary to execute.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    compiler = args.compiler.resolve()
    if not compiler.exists():
        print(f"Compiler not found: {compiler}", file=sys.stderr)
        return 2

    sources = discover_sources(REPO_ROOT)
    if not sources:
        print("No seed .dl sources found under tests/", file=sys.stderr)
        return 2

    FUZZ_DIR.mkdir(parents=True, exist_ok=True)

    seed = args.seed if args.seed is not None else random.randrange(2**32)
    rng = random.Random(seed)

    print(f"seed={seed}")
    print(f"sources={len(sources)}")
    print(f"iterations={args.iterations}")
    print(f"working_dir={FUZZ_DIR}")

    for iteration in range(1, args.iterations + 1):
        source = rng.choice(sources)
        original = read_text(source)
        mutation_count = max(1, args.mutations + rng.randint(0, 4))
        mutated = mutate_source(original, rng, mutation_count)

        write_text(CURRENT_INPUT, mutated)
        output_path = FUZZ_DIR / "current_output"

        try:
            result = compile_case(compiler, CURRENT_INPUT, output_path, args.timeout)
        except subprocess.TimeoutExpired as exc:
            shutil.copyfile(CURRENT_INPUT, CRASH_INPUT)
            write_text(CURRENT_STDOUT, exc.stdout or "")
            write_text(CURRENT_STDERR, exc.stderr or "")
            write_text(
                CRASH_META,
                "\n".join(
                    [
                        f"status=timeout",
                        f"iteration={iteration}",
                        f"seed={seed}",
                        f"base_source={source.relative_to(REPO_ROOT)}",
                        f"mutations={mutation_count}",
                        f"input={CRASH_INPUT}",
                    ]
                )
                + "\n",
            )
            print(f"[{iteration}/{args.iterations}] timeout on {source.relative_to(REPO_ROOT)}")
            print(f"saved crash input to {CRASH_INPUT}")
            return 1

        write_text(CURRENT_STDOUT, result.stdout)
        write_text(CURRENT_STDERR, result.stderr)

        if result.returncode < 0:
            shutil.copyfile(CURRENT_INPUT, CRASH_INPUT)
            write_text(
                CRASH_META,
                "\n".join(
                    [
                        f"status=signal",
                        f"signal={signal_name(result.returncode)}",
                        f"iteration={iteration}",
                        f"seed={seed}",
                        f"base_source={source.relative_to(REPO_ROOT)}",
                        f"mutations={mutation_count}",
                        f"input={CRASH_INPUT}",
                    ]
                )
                + "\n",
            )
            print(
                f"[{iteration}/{args.iterations}] compiler crashed with {signal_name(result.returncode)} "
                f"while mutating {source.relative_to(REPO_ROOT)}"
            )
            print(f"saved crash input to {CRASH_INPUT}")
            return 1

        if iteration == 1 or iteration % 50 == 0:
            print(f"[{iteration}/{args.iterations}] last source={source.relative_to(REPO_ROOT)} exit={result.returncode}")

    print("No compiler crash found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
