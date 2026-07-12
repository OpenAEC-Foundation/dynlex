#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path


IMPORT_RE = re.compile(r"^\s*import\s+([^\s#]+)")
EXCLUDED_DIRECTORY_NAMES = {".cache", ".git", ".idea", ".vscode", "node_modules"}


def discover_dl_files(repo_root: Path) -> list[Path]:
    files: list[Path] = []
    for path in repo_root.rglob("*.dl"):
        relative_parts = path.relative_to(repo_root).parts
        if any(part in EXCLUDED_DIRECTORY_NAMES or part.startswith("build") for part in relative_parts):
            continue
        files.append(path.resolve())
    return sorted(files)


def normalize_import_token(token: str) -> str:
    token = token.strip()
    if (token.startswith('"') and token.endswith('"')) or (token.startswith("'") and token.endswith("'")):
        token = token[1:-1]
    return token


def resolve_import_path(repo_root: Path, importer: Path, token: str) -> Path | None:
    token = normalize_import_token(token)
    if not token:
        return None

    bases = [importer.parent / token, repo_root / token]
    if "/" not in token and "\\" not in token:
        bases.append(repo_root / "lib" / token)

    for base in bases:
        candidates = [base]
        if base.suffix == "":
            candidates.append(base.with_suffix(".dl"))
        for candidate in candidates:
            resolved = candidate.resolve()
            if resolved.is_file() and resolved.suffix == ".dl":
                return resolved
    return None


def build_import_graph(repo_root: Path, files: list[Path]) -> tuple[dict[Path, set[Path]], list[tuple[Path, str]]]:
    file_set = set(files)
    graph: dict[Path, set[Path]] = {path: set() for path in files}
    unresolved: list[tuple[Path, str]] = []
    for importer in files:
        for line in importer.read_text(encoding="utf-8", errors="replace").splitlines():
            match = IMPORT_RE.match(line)
            if not match:
                continue
            token = match.group(1)
            imported = resolve_import_path(repo_root, importer, token)
            if imported is None or imported not in file_set:
                unresolved.append((importer, normalize_import_token(token)))
                continue
            graph[importer].add(imported)
    return graph, unresolved


def discover_entry_points(files: list[Path], graph: dict[Path, set[Path]]) -> list[Path]:
    imported = {path for imports in graph.values() for path in imports}
    return [path for path in files if path not in imported]


def reachable_files(roots: list[Path], graph: dict[Path, set[Path]]) -> set[Path]:
    reachable: set[Path] = set()
    stack = list(roots)
    while stack:
        current = stack.pop()
        if current in reachable:
            continue
        reachable.add(current)
        stack.extend(graph[current])
    return reachable
