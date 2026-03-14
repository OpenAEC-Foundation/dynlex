#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


IMPORT_RE = re.compile(r"^\s*import\s+([^\s#]+)")
PERF_LINE_RE = re.compile(r"^\s*(\d+)\s*;\s*(.*?)\s*;\s*(.*?)\s*$")


@dataclass
class ProfileFailure:
    main_file: Path
    return_code: int
    reason: str


def run_checked(args: list[str], cwd: Path, capture_output: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=str(cwd),
        text=True,
        capture_output=capture_output,
        check=False,
    )


def first_relevant_error(stderr_text: str) -> str:
    lines = [line.strip() for line in stderr_text.splitlines() if line.strip()]
    if not lines:
        return "unknown failure"
    for line in lines:
        if "Error:" in line:
            return line
    for line in lines:
        if "couldn't import" in line.lower():
            return line
    return lines[-1]


def normalize_import_token(token: str) -> str:
    token = token.strip()
    if (token.startswith('"') and token.endswith('"')) or (token.startswith("'") and token.endswith("'")):
        token = token[1:-1]
    return token


def resolve_import_path(repo_root: Path, importer: Path, token: str) -> Path | None:
    token = normalize_import_token(token)
    if not token:
        return None

    base_candidates = [
        importer.parent / token,
        repo_root / token,
    ]
    if "/" not in token and "\\" not in token:
        base_candidates.append(repo_root / "lib" / token)

    candidates: list[Path] = []
    for base in base_candidates:
        candidates.append(base)
        if base.suffix == "":
            candidates.append(base.with_suffix(".dl"))

    for candidate in candidates:
        candidate = candidate.resolve()
        if candidate.exists() and candidate.suffix == ".dl":
            return candidate
    return None


def discover_main_files(repo_root: Path) -> list[Path]:
    excluded_dirs = {"build", ".git", ".cache", ".idea", ".vscode"}
    all_dl: list[Path] = []
    for p in repo_root.rglob("*.dl"):
        rel_parts = p.relative_to(repo_root).parts
        if any(part in excluded_dirs for part in rel_parts):
            continue
        all_dl.append(p.resolve())
    all_dl.sort()
    imported: set[Path] = set()

    for dl_file in all_dl:
        try:
            lines = dl_file.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            match = IMPORT_RE.match(line)
            if not match:
                continue
            resolved = resolve_import_path(repo_root, dl_file, match.group(1))
            if resolved is not None:
                imported.add(resolved)

    mains = [p for p in all_dl if p not in imported]
    return mains


def detect_compiler(repo_root: Path, explicit: str | None) -> Path:
    if explicit:
        compiler = (repo_root / explicit).resolve() if not os.path.isabs(explicit) else Path(explicit).resolve()
        if not compiler.exists():
            raise FileNotFoundError(f"compiler not found: {compiler}")
        return compiler

    candidates = [repo_root / "build" / "dynlex", repo_root / "build" / "dynlex.exe"]
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate.resolve()
    raise FileNotFoundError("compiler not found; expected build/dynlex or build/dynlex.exe")


def demangle(symbol: str, cache: dict[str, str]) -> str:
    if symbol in cache:
        return cache[symbol]
    cleaned = symbol.strip()
    if cleaned.startswith("[.] "):
        cleaned = cleaned[4:]
    if cleaned.startswith("_Z"):
        proc = subprocess.run(["c++filt", "-n", cleaned], text=True, capture_output=True, check=False)
        if proc.returncode == 0 and proc.stdout.strip():
            cache[symbol] = proc.stdout.strip()
            return cache[symbol]
    cache[symbol] = cleaned
    return cleaned


def simplify_name(demangled: str) -> str:
    text = demangled.strip()
    text = re.sub(r"\s+", " ", text)
    if "(" in text:
        text = text.split("(", 1)[0].strip()
    text = re.sub(r"<[^<>]*(?:<[^<>]*>[^<>]*)*>", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text or demangled


def is_project_owned_function(demangled_name: str) -> bool:
    text = demangled_name.strip()
    if not text:
        return False

    if "@plt" in text:
        return False
    if text.startswith("_Z"):
        return False
    if text.startswith("__"):
        return False
    if text.startswith("TLS wrapper function for "):
        return False
    if text.startswith("TLS init function for "):
        return False

    excluded_namespace_patterns = (
        r"(^|[\s:<(,])std::",
        r"(^|[\s:<(,])__gnu_cxx::",
        r"(^|[\s:<(,])__cxxabiv1::",
        r"(^|[\s:<(,])__pstl::",
        r"(^|[\s:<(,])llvm::",
    )
    for pattern in excluded_namespace_patterns:
        if re.search(pattern, text):
            return False

    # Mangled forms that may fail demangling still identify standard/LLVM/runtime code.
    excluded_mangled_prefixes = (
        "_ZSt",        # std::
        "_ZNSt",       # std::
        "_ZN9__gnu_cxx",
        "_ZN4llvm",
        "_ZNK4llvm",
        "_ZN12_GLOBAL__N_1",  # anonymous namespace in non-project static libs
    )
    for prefix in excluded_mangled_prefixes:
        if text.startswith(prefix):
            return False

    excluded_substrings = (
        "operator new",
        "operator delete",
        "typeinfo for ",
        "vtable for ",
        "non-virtual thunk to ",
        "virtual thunk to ",
        "(anonymous namespace)::",
        "_GLOBAL__sub_I_",
        "doinsert",
    )
    for piece in excluded_substrings:
        if piece in text:
            return False

    # Keep anonymous-namespace and project functions, including C-style helpers.
    return True


def contains_banned_symbol_pattern(symbol_name: str) -> bool:
    banned_patterns = (
        r"std::",
        r"__gnu_cxx::",
        r"__cxxabiv1::",
        r"__pstl::",
        r"llvm::",
        r"@plt",
        r"^_Z[A-Za-z0-9_]+",
        r"_GLOBAL__sub_I_",
        r"\(anonymous namespace\)::",
        r"\bdoinsert\b",
    )
    return any(re.search(pattern, symbol_name) for pattern in banned_patterns)


def parse_perf_hits(perf_report_output: str) -> Iterable[tuple[int, str, str]]:
    for line in perf_report_output.splitlines():
        match = PERF_LINE_RE.match(line)
        if not match:
            continue
        try:
            samples = int(match.group(1))
        except ValueError:
            continue
        symbol = match.group(2).strip()
        dso = match.group(3).strip()
        if samples <= 0 or not symbol:
            continue
        yield samples, symbol, dso


def partition_layout(items: list[dict[str, object]], x: float, y: float, w: float, h: float, vertical: bool) -> list[dict[str, object]]:
    if not items:
        return []
    if len(items) == 1:
        item = dict(items[0])
        item.update({"x": x, "y": y, "w": w, "h": h})
        return [item]

    total = sum(float(i["hits"]) for i in items)
    left_sum = 0.0
    split_idx = 0
    for idx, item in enumerate(items):
        left_sum += float(item["hits"])
        split_idx = idx
        if left_sum >= total / 2:
            break
    left_items = items[: split_idx + 1]
    right_items = items[split_idx + 1 :]

    if not right_items:
        right_items = [left_items.pop()]
        left_sum = sum(float(i["hits"]) for i in left_items)
        if left_sum <= 0:
            left_sum = total / 2

    ratio = left_sum / total if total > 0 else 0.5

    if vertical:
        w_left = w * ratio
        return partition_layout(left_items, x, y, w_left, h, not vertical) + partition_layout(
            right_items, x + w_left, y, w - w_left, h, not vertical
        )

    h_top = h * ratio
    return partition_layout(left_items, x, y, w, h_top, not vertical) + partition_layout(
        right_items, x, y + h_top, w, h - h_top, not vertical
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Profile all non-imported .dl files and build an interactive function-hit treemap."
    )
    parser.add_argument("--compiler", help="Path to dynlex compiler executable")
    parser.add_argument("--output", default="docs/profiling/main_function_hits_heatmap.html", help="Output HTML file")
    parser.add_argument("--tmp-dir", default="/tmp/dynlex_main_hits", help="Temporary directory for perf data")
    parser.add_argument("--frequency", type=int, default=999, help="perf sampling frequency")
    parser.add_argument("--max-mains", type=int, default=0, help="Limit number of mains to profile (0 = all)")
    parser.add_argument("--include-system", action="store_true", help="Include non-compiler DSOs")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    compiler = detect_compiler(repo_root, args.compiler)
    tmp_dir = Path(args.tmp_dir).resolve()
    tmp_dir.mkdir(parents=True, exist_ok=True)
    out_path = (repo_root / args.output).resolve() if not os.path.isabs(args.output) else Path(args.output).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    mains = discover_main_files(repo_root)
    if args.max_mains > 0:
        mains = mains[: args.max_mains]
    if not mains:
        print("No main .dl files discovered.")
        return 1

    compiler_name = compiler.name
    demangle_cache: dict[str, str] = {}
    total_hits: dict[str, int] = {}
    representative: dict[str, str] = {}
    main_hit_totals: dict[str, int] = {}
    failures: list[ProfileFailure] = []

    for idx, main_file in enumerate(mains):
        perf_data = tmp_dir / f"profile_{idx}.data"
        output_bin = tmp_dir / f"profile_{idx}.out"
        rel_main = str(main_file.relative_to(repo_root))
        print(f"[{idx + 1}/{len(mains)}] profiling {rel_main}")

        compile_cmd = [str(compiler), rel_main, "-o", str(output_bin)]

        # Preflight without perf so non-standalone files are excluded cleanly.
        preflight_proc = run_checked(compile_cmd, repo_root)
        if preflight_proc.returncode != 0:
            failures.append(
                ProfileFailure(
                    main_file=main_file,
                    return_code=preflight_proc.returncode,
                    reason=first_relevant_error(preflight_proc.stderr or ""),
                )
            )
            continue

        perf_cmd = ["perf", "record", "-o", str(perf_data), "-F", str(args.frequency), "-g", "--"] + compile_cmd
        record_proc = run_checked(perf_cmd, repo_root)
        if record_proc.returncode != 0:
            failures.append(
                ProfileFailure(
                    main_file=main_file,
                    return_code=record_proc.returncode,
                    reason=first_relevant_error(record_proc.stderr or ""),
                )
            )
            continue

        report_cmd = [
            "perf",
            "report",
            "-i",
            str(perf_data),
            "--stdio",
            "--no-children",
            "--call-graph",
            "none",
            "--no-demangle",
            "-F",
            "sample,symbol,dso",
            "-t",
            ";",
            "--percent-limit",
            "0.01",
        ]
        report_proc = run_checked(report_cmd, repo_root)
        if report_proc.returncode != 0:
            failures.append(
                ProfileFailure(
                    main_file=main_file,
                    return_code=report_proc.returncode,
                    reason=first_relevant_error(report_proc.stderr or ""),
                )
            )
            continue

        file_hits = 0
        for samples, symbol, dso in parse_perf_hits(report_proc.stdout):
            if not args.include_system:
                if Path(dso).name != compiler_name and dso != compiler_name:
                    continue
            full_name = demangle(symbol, demangle_cache)
            if not is_project_owned_function(full_name):
                continue
            short_name = simplify_name(full_name)
            total_hits[short_name] = total_hits.get(short_name, 0) + samples
            representative.setdefault(short_name, full_name)
            file_hits += samples
        main_hit_totals[rel_main] = file_hits

    ranked = sorted(total_hits.items(), key=lambda kv: kv[1], reverse=True)
    top_for_layout = ranked[:500]
    if not top_for_layout:
        print("No function hits collected.")
        return 1

    offenders = [name for name, _hits in ranked if contains_banned_symbol_pattern(name)]
    if offenders:
        sample = "\n".join(offenders[:20])
        raise RuntimeError(f"banned symbols leaked into report:\n{sample}")

    layout_items = [{"name": name, "hits": hits, "full": representative.get(name, name)} for name, hits in top_for_layout]
    rectangles = partition_layout(layout_items, 0.0, 0.0, 100.0, 100.0, vertical=True)
    max_hits = max(item["hits"] for item in layout_items)
    total_hit_count = sum(total_hits.values())

    rect_json = json.dumps(
        [
            {
                "name": str(r["name"]),
                "full": str(r["full"]),
                "hits": int(r["hits"]),
                "x": round(float(r["x"]), 6),
                "y": round(float(r["y"]), 6),
                "w": round(float(r["w"]), 6),
                "h": round(float(r["h"]), 6),
            }
            for r in rectangles
        ],
        ensure_ascii=True,
    )

    top_rows_html = []
    for name, hits in ranked[:300]:
        full_name = representative.get(name, name)
        top_rows_html.append(
            "<tr>"
            f"<td>{html.escape(name)}</td>"
            f"<td>{hits}</td>"
            f"<td class=\"mono\">{html.escape(full_name)}</td>"
            "</tr>"
        )

    main_rows_html = []
    for rel_main, hits in sorted(main_hit_totals.items(), key=lambda kv: kv[1], reverse=True):
        main_rows_html.append(f"<tr><td>{html.escape(rel_main)}</td><td>{hits}</td></tr>")

    failure_rows_html = []
    for fail in failures:
        rel_path = str(fail.main_file.relative_to(repo_root))
        failure_rows_html.append(
            "<tr>"
            f"<td>{html.escape(rel_path)}</td>"
            f"<td>{fail.return_code}</td>"
            f"<td class=\"mono\">{html.escape(fail.reason)}</td>"
            "</tr>"
        )

    html_out = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>DynLex Main File Function Hit Heatmap</title>
  <style>
    body {{
      margin: 0;
      font-family: "IBM Plex Sans", "Segoe UI", Arial, sans-serif;
      background: #f4f7fb;
      color: #0f2440;
    }}
    main {{
      max-width: 1400px;
      margin: 18px auto 40px;
      padding: 0 14px;
    }}
    h1 {{ margin: 0 0 6px; }}
    .muted {{ color: #576a89; font-size: 0.92rem; }}
    .grid {{
      display: grid;
      grid-template-columns: 2fr 1fr;
      gap: 12px;
      margin-top: 12px;
    }}
    .card {{
      background: #fff;
      border: 1px solid #d6deea;
      border-radius: 10px;
      padding: 12px;
      box-shadow: 0 8px 22px rgba(15, 36, 64, 0.05);
    }}
    #heatmap {{
      position: relative;
      width: 100%;
      aspect-ratio: 16 / 10;
      border: 1px solid #cfd8e8;
      border-radius: 8px;
      overflow: hidden;
      background: #f8fbff;
    }}
    .rect {{
      position: absolute;
      border: 1px solid rgba(255,255,255,0.8);
      box-sizing: border-box;
      cursor: default;
    }}
    .label {{
      position: absolute;
      left: 4px;
      top: 2px;
      right: 2px;
      font-size: 10px;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      color: #0b1e36;
      pointer-events: none;
    }}
    #tooltip {{
      position: fixed;
      display: none;
      background: #10233f;
      color: #f0f5ff;
      font-size: 12px;
      padding: 6px 8px;
      border-radius: 6px;
      pointer-events: none;
      z-index: 9999;
      max-width: 560px;
      box-shadow: 0 8px 18px rgba(0,0,0,0.25);
      white-space: pre-wrap;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      font-size: 0.84rem;
    }}
    th, td {{
      border-bottom: 1px solid #e1e7f1;
      text-align: left;
      padding: 6px 5px;
      vertical-align: top;
    }}
    th {{
      font-size: 0.75rem;
      letter-spacing: 0.02em;
      text-transform: uppercase;
      color: #556784;
      background: #f9fbff;
      position: sticky;
      top: 0;
    }}
    .scroll {{
      max-height: 520px;
      overflow: auto;
      border: 1px solid #d6deea;
      border-radius: 8px;
    }}
    .mono {{
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 0.75rem;
      word-break: break-word;
    }}
  </style>
</head>
<body>
<main>
  <h1>Function Hit Heatmap Across Main .dl Files</h1>
  <div class="muted">
    Mains discovered: <b>{len(mains)}</b> |
    Mains profiled successfully: <b>{len(main_hit_totals)}</b> |
    Failed: <b>{len(failures)}</b> |
    Aggregated compiler-function hits: <b>{total_hit_count}</b> |
    Frequency: <b>{args.frequency}</b>
  </div>

  <div class="grid">
    <section class="card">
      <h2 style="margin:0 0 8px;">Treemap (Top 500 Functions by Hit Count)</h2>
      <div id="heatmap"></div>
      <p class="muted" style="margin:8px 0 0;">
        Hover a rectangle to see function and exact hit count. Rectangle area is proportional to hit count.
      </p>
    </section>

    <section class="card">
      <h2 style="margin:0 0 8px;">Top Main Files by Compiler Hits</h2>
      <div class="scroll">
        <table>
          <thead><tr><th>Main File</th><th>Hits</th></tr></thead>
          <tbody>
            {''.join(main_rows_html)}
          </tbody>
        </table>
      </div>
    </section>
  </div>

  <section class="card" style="margin-top:12px;">
    <h2 style="margin:0 0 8px;">Top 300 Functions (Aggregated Hits)</h2>
    <div class="scroll">
      <table>
        <thead><tr><th>Function</th><th>Hits</th><th>Representative Full Name</th></tr></thead>
        <tbody>
          {''.join(top_rows_html)}
        </tbody>
      </table>
    </div>
  </section>

  <section class="card" style="margin-top:12px;">
    <h2 style="margin:0 0 8px;">Profiling Failures</h2>
    <div class="scroll">
      <table>
        <thead><tr><th>Main File</th><th>Exit</th><th>Stderr Tail</th></tr></thead>
        <tbody>
          {''.join(failure_rows_html) if failure_rows_html else '<tr><td colspan="3">None</td></tr>'}
        </tbody>
      </table>
    </div>
  </section>
</main>
<div id="tooltip"></div>
<script>
  const rectangles = {rect_json};
  const maxHits = {max_hits};

  const heatmap = document.getElementById("heatmap");
  const tooltip = document.getElementById("tooltip");

  function colorFor(hits) {{
    const t = Math.log(hits + 1) / Math.log(maxHits + 1);
    const hue = 210 - Math.round(180 * t);
    const sat = 75;
    const light = 88 - Math.round(40 * t);
    return `hsl(${{hue}} ${{sat}}% ${{light}}%)`;
  }}

  function maybeLabel(rect, name) {{
    if (rect.w < 9 || rect.h < 6) {{
      return;
    }}
    const label = document.createElement("div");
    label.className = "label";
    label.textContent = name;
    rect.el.appendChild(label);
  }}

  rectangles.forEach((item) => {{
    const el = document.createElement("div");
    el.className = "rect";
    el.style.left = `${{item.x}}%`;
    el.style.top = `${{item.y}}%`;
    el.style.width = `${{item.w}}%`;
    el.style.height = `${{item.h}}%`;
    el.style.background = colorFor(item.hits);
    maybeLabel({{ w: item.w, h: item.h, el }}, item.name);

    el.addEventListener("mouseenter", () => {{
      tooltip.style.display = "block";
      tooltip.textContent = `${{item.name}}\\nHits: ${{item.hits}}\\nFull: ${{item.full}}`;
    }});
    el.addEventListener("mousemove", (ev) => {{
      tooltip.style.left = `${{ev.clientX + 14}}px`;
      tooltip.style.top = `${{ev.clientY + 14}}px`;
    }});
    el.addEventListener("mouseleave", () => {{
      tooltip.style.display = "none";
    }});

    heatmap.appendChild(el);
  }});
</script>
</body>
</html>
"""

    out_path.write_text(html_out, encoding="utf-8")
    print(f"wrote: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
