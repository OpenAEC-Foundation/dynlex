#!/usr/bin/env python3
"""Compare two compilers on the same frozen, real LEXI8 Breakout application.

The source revision is explicit so compiler comparisons cannot silently change
the application. Programs are compiled but never launched. CPU accounting is
available on POSIX; elapsed time is measured on every supported Python platform.
"""

import argparse
import hashlib
import io
import json
import os
import platform
import random
import shutil
import statistics
import subprocess
import tarfile
import tempfile
import time
from pathlib import Path

if os.name == "posix":
    import resource


REPOSITORY = Path(__file__).resolve().parent.parent
SOURCE_REVISION = "d913a7db39d7705b738aeab0db6204ecc801f352"
CASES = ("unchanged", "comment", "wording", "unused", "body", "lives", "shared-type", "cold")
FAST_CASES = {"unchanged", "comment", "wording"}


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def child_cpu_seconds():
    if os.name != "posix":
        return None
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def source_hashes(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob("*.dl"))}


def stage_application(root, revision):
    archive = subprocess.check_output(
        ["git", "archive", revision, "lib", "tests/games/lexi8"], cwd=REPOSITORY)
    with tarfile.open(fileobj=io.BytesIO(archive)) as contents:
        for member in contents:
            relative = Path(member.name)
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError(f"unsafe application archive path: {member.name}")
            if member.isdir():
                continue
            if not member.isfile():
                raise RuntimeError(f"application archive contains a non-file: {member.name}")
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents.extractfile(member).read())


def replace_once(data, before, after):
    if data.count(before) != 1:
        raise RuntimeError(f"benchmark edit site must occur exactly once: {before!r}")
    return data.replace(before, after)


def prepare_case(root, originals, case, sample=None):
    for path, data in originals.items():
        path.write_bytes(data)
    config = root / "config.dl"
    if config.exists():
        config.unlink()
    game = root / "tests/games/lexi8"
    main, chip, breakout = (game / name for name in ("main.dl", "chip8.dl", "breakout.dl"))
    if case == "wording":
        label = "First" if sample is None else f"Updated {sample}"
        config.write_text('dynlex options:\n    messages:\n'
                          f'        ambiguous operand grouping: "{label} wording for {{expression}}"\n')
    if sample is None:
        return
    if case == "comment":
        chip.write_bytes(f"# Incremental comparison {sample}\n".encode() + originals[chip])
    elif case == "body":
        main.write_bytes(replace_once(originals[main], b"98.0 * brightness",
                                     f"{97-sample}.0 * brightness".encode()))
    elif case == "unused":
        strings = root / "lib/string.dl"
        strings.write_bytes(replace_once(originals[strings],
            b'set separator to the string form of ", "',
            f'set separator to the string form of "; {sample} "'.encode()))
    elif case == "lives":
        breakout.write_bytes(replace_once(originals[breakout],
            b"load value 3 into register registers's lives",
            f"load value {2+sample*2} into register registers's lives".encode()))
    elif case == "shared-type":
        width = 64 if sample % 2 == 0 else 32
        edited = replace_once(originals[chip], b"and cycle count {integer:cycles} means:",
            f'and cycle count {{@intrinsic("type", "int", {width}):cycles}} means:'.encode())
        chip.write_bytes(replace_once(edited, b"and cycle count 0\n",
            f'and cycle count (@intrinsic("cast", 0, @intrinsic("type", "int", {width})))\n'.encode()))


def ratio_interval(ratios):
    if len(ratios) < 3:
        return None
    generator = random.Random(0)
    bootstraps = sorted(statistics.median(generator.choices(ratios, k=len(ratios)))
                        for _ in range(2000))
    return [bootstraps[49], bootstraps[1949]]


def summarize(rows, cases):
    result = []
    for case in cases:
        measured = [r for r in rows if r["case"] == case and r["phase"] == "measured"]
        summary = {"case": case, "pairs": len(measured) // 2}
        for metric in ("wall_seconds", "cpu_seconds"):
            groups = {label: [r for r in measured if r["compiler"] == label]
                      for label in ("baseline", "candidate")}
            if groups["baseline"][0][metric] is None:
                summary[metric] = None
                continue
            before = {r["sample"]: r[metric] for r in groups["baseline"]}
            after = {r["sample"]: r[metric] for r in groups["candidate"]}
            ratios = [after[index] / before[index] for index in sorted(before)]
            summary[metric] = {
                "baseline_median": statistics.median(before.values()),
                "candidate_median": statistics.median(after.values()),
                "paired_ratio_median": statistics.median(ratios),
                "paired_ratio_interval_95": ratio_interval(ratios),
            }
        result.append(summary)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-revision", default=SOURCE_REVISION)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--repetitions", type=int, default=7)
    parser.add_argument("--fast-repetitions", type=int, default=15)
    parser.add_argument("--cold-repetitions", type=int, default=3)
    parser.add_argument("--compile-timeout", type=int, default=30)
    parser.add_argument("--require-identical-executable", action="store_true")
    args = parser.parse_args()
    if not all(1 <= count <= 64 for count in
               (args.repetitions, args.fast_repetitions, args.cold_repetitions)):
        parser.error("sample counts must be between 1 and 64")
    binaries = {label: path.resolve() for label, path in
                (("baseline", args.baseline), ("candidate", args.candidate))}
    if any(not path.is_file() for path in binaries.values()):
        parser.error("both prebuilt compiler executables must exist")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_parent = REPOSITORY / ".cache" / "comparison-work"
    temporary_parent.mkdir(parents=True, exist_ok=True)
    rows = []
    report = {
        "program": "LEXI8 Breakout",
        "source_revision": subprocess.check_output(
            ["git", "rev-parse", args.source_revision], cwd=REPOSITORY, text=True).strip(),
        "compiler_sha256": {label: digest(path) for label, path in binaries.items()},
        "platform": platform.platform(),
        "cpu_accounting": "POSIX RUSAGE_CHILDREN" if os.name == "posix" else "unavailable",
        "options": vars(args) | {"baseline": str(binaries["baseline"]),
                                 "candidate": str(binaries["candidate"]), "output": str(output)},
        "complete": False, "rows": rows,
    }

    def save():
        output.write_text(json.dumps(report, indent=2, default=str) + "\n")

    save()
    with tempfile.TemporaryDirectory(prefix="paired-", dir=temporary_parent) as temporary:
        workspace = Path(temporary)
        slots = {"baseline": "a", "candidate": "b"}
        executables = {}
        for label, binary in binaries.items():
            staged = workspace / "bin" / slots[label] / ("dynlex.exe" if os.name == "nt" else "dynlex")
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, staged)
            if digest(staged) != report["compiler_sha256"][label]:
                raise RuntimeError("staged compiler differs from the requested executable")
            subprocess.run([str(staged), "--version"], check=True, capture_output=True,
                           text=True, timeout=args.compile_timeout)
            executables[label] = staged
        root = workspace / "application"
        stage_application(root, args.source_revision)
        game = root / "tests/games/lexi8"
        entry = game / "main.dl"
        originals = {path: path.read_bytes()
                     for path in (entry, game / "chip8.dl", game / "breakout.dl", root / "lib/string.dl")}
        report["original_source_sha256"] = source_hashes(root)

        def compile_one(label, case, sample, phase, incremental):
            destination = workspace / slots[label] / case
            destination.mkdir(parents=True, exist_ok=True)
            executable = destination / ("program" + (".exe" if os.name == "nt" else ".out"))
            command = [str(executables[label]), str(entry), "-O2", "-o", str(executable)]
            command += ["--cache-dir", str(destination / "cache"), "--incremental-stats"] \
                if incremental else ["--no-incremental"]
            before = child_cpu_seconds()
            started = time.perf_counter()
            process = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                     timeout=args.compile_timeout)
            elapsed = time.perf_counter() - started
            after = child_cpu_seconds()
            diagnostics = "".join(line for line in process.stderr.splitlines(keepends=True)
                                  if not line.startswith("incremental: "))
            stats = next((json.loads(line[13:]) for line in process.stderr.splitlines()
                          if line.startswith("incremental: ")), None)
            row = {"compiler": label, "case": case, "sample": sample, "phase": phase,
                   "command": command, "returncode": process.returncode,
                   "wall_seconds": elapsed, "cpu_seconds": after - before if before is not None else None,
                   "diagnostics": diagnostics, "stats": stats,
                   "source_sha256": source_hashes(root),
                   "executable_sha256": digest(executable) if process.returncode == 0 else None}
            rows.append(row)
            save()
            if process.returncode:
                raise RuntimeError(f"{label} {case} failed; timing is invalid:\n{diagnostics}")
            if incremental and stats is None:
                raise RuntimeError("the compiler did not report incremental statistics")
            print(f"{case} {phase} {sample} {label}: {elapsed:.4f} wall seconds", flush=True)
            return row

        for case in args.cases:
            prepare_case(root, originals, case)
            if case != "cold":
                for label in binaries:
                    compile_one(label, case, -1, "populate", True)
            count = args.fast_repetitions if case in FAST_CASES else \
                args.cold_repetitions if case == "cold" else args.repetitions
            for sample in range(count):
                prepare_case(root, originals, case, sample)
                hashes = source_hashes(root)
                order = ("baseline", "candidate") if sample % 2 == 0 else ("candidate", "baseline")
                pair = {label: compile_one(label, case, sample, "measured", case != "cold") for label in order}
                if source_hashes(root) != hashes:
                    raise RuntimeError("a compiler invocation modified the application inputs")
                for row in pair.values():
                    row["source_sha256"] = hashes
                if pair["baseline"]["diagnostics"] != pair["candidate"]["diagnostics"]:
                    raise RuntimeError(f"baseline/candidate diagnostics differ: {case}")
                if case == "unused" and any(row["stats"]["objects_built"] or row["stats"]["link_ms"]
                                            for row in pair.values()):
                    raise RuntimeError("the inactive-function edit rebuilt executable code")
                if args.require_identical_executable and \
                        pair["baseline"]["executable_sha256"] != pair["candidate"]["executable_sha256"]:
                    raise RuntimeError(f"baseline/candidate executable artifacts differ: {case}")
                save()
            if case != "cold":
                for label in binaries:
                    clean = compile_one(label, case, count, "clean-parity", False)
                    if clean["diagnostics"] != pair[label]["diagnostics"]:
                        raise RuntimeError(f"incremental/clean diagnostics differ: {case}, {label}")
        report["summary"] = summarize(rows, args.cases)
        empty_cases = sorted({r["case"] for r in rows if r["phase"] == "populate" and
                              r["source_sha256"] == report["original_source_sha256"]})
        if empty_cases:
            empty_rows = [r | {"case": "empty-default-cache", "phase": "measured",
                               "sample": empty_cases.index(r["case"])}
                          for r in rows if r["phase"] == "populate" and r["case"] in empty_cases]
            report["empty_cache_summary"] = summarize(empty_rows, ["empty-default-cache"])
        report["compiler_unchanged"] = all(digest(path) == report["compiler_sha256"][label]
                                           for label, path in binaries.items())
        if not report["compiler_unchanged"]:
            raise RuntimeError("a compiler executable changed during the comparison")
        report["complete"] = True
        save()
    print(json.dumps(report["summary"], indent=2), flush=True)


if __name__ == "__main__":
    main()
