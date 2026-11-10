"""Stage a real dynlex-ui game beside the DynLex compiler's standard library."""

import shutil
import subprocess
from pathlib import Path


def ui_repository(project: Path) -> Path:
    common = subprocess.check_output(
        ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
        cwd=project, text=True,
    ).strip()
    ui = Path(common).resolve().parent.parent / "dynlex-ui"
    if not (ui / "examples" / "games").is_dir():
        raise SystemExit(f"dynlex-ui game sources are unavailable at {ui}")
    return ui


def stage_game(project: Path, ui: Path, game: str, root: Path) -> Path:
    shutil.copytree(project / "lib", root / "lib")
    shutil.copytree(ui / "ui", root / "ui")
    shutil.copytree(ui / "platform", root / "platform")
    source = Path("examples") / "games" / game
    shutil.copytree(ui / "examples" / "games", root / "examples" / "games")
    return source
