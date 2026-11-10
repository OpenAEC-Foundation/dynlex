#!/usr/bin/env python3
"""Wide integer specialization, cache persistence and language-server values."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from lsp_tokens import LspSession, initialize_session, to_file_uri


compiler = Path(sys.argv[1]).resolve()
project = Path(__file__).resolve().parent.parent
high = 1 << 100
source = f'''import lib/std.dl
to get bumped {{fixed a 128 bit integer:value}}:
    return value + 1
to get identity {{a 128 bit integer:value}}:
    return value
set high to {high}
set negative to -{high}
print line bumped high
print line bumped (high + 1)
print line bumped negative
print line identity high
print line identity negative
print line "first"
'''
expected = f"{high + 1}\n{high + 2}\n{-high + 1}\n{high}\n{-high}\nfirst\n"

with tempfile.TemporaryDirectory(prefix="dynlex-wide-values-") as directory:
    root = Path(directory)
    entry = root / "main.dl"
    executable = root / ("program.exe" if sys.platform == "win32" else "program.out")
    cache = root / "cache"

    def build(text, optimization, cached=False):
        entry.write_text(text)
        command = [str(compiler), str(entry), optimization, "-o", str(executable)]
        if cached:
            command.extend(["--incremental", str(cache), "--incremental-stats"])
        result = subprocess.run(command, cwd=project, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
        assert run.returncode == 0, run.stderr
        return run.stdout, result.stderr

    for optimization in ("-O0", "-O2"):
        assert build(source, optimization)[0] == expected
    assert build(source, "-O0", True)[0] == expected
    assert build(source, "-O0", True)[0] == expected
    edited = source.replace('"first"', '"second"')
    assert build(edited, "-O0", True)[0] == expected.replace("first", "second")
    edited = edited.replace(f"set high to {high}", f"set high to {high + 4}")
    assert build(edited, "-O0", True)[0] == build(edited, "-O0")[0]

    entry.write_text(source)
    uri = to_file_uri(entry)
    session = LspSession(compiler, project, False, False)
    try:
        initialize_session(session, project)
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "dynlex", "version": 1, "text": source
        }})
        session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        # Check a source literal whose complete value is available to the LSP.
        line = source.splitlines().index(f"set high to {high}")
        hover = session.request("textDocument/hover", {
            "textDocument": {"uri": uri}, "position": {"line": line, "character": 13}
        })
        assert str(high) in json.dumps(hover), hover
        assert "128" in json.dumps(hover), hover
    finally:
        session.close()

print("Wide integer specializations, native caches and editor values remain exact.")
