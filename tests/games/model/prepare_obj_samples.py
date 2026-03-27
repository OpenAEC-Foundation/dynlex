#!/usr/bin/env python3

from __future__ import annotations

import shutil
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "assets" / "samples" / "source"
CHOICES = ROOT / "assets" / "samples" / "choices"


SAMPLE_SPECS = [
    {
        "id": "1",
        "name": "commercial_tower_j",
        "label": "Commercial Tower J",
        "obj": SOURCE / "commercial" / "Models" / "OBJ format" / "building-j.obj",
    },
    {
        "id": "2",
        "name": "suburban_house_t",
        "label": "Suburban House T",
        "obj": SOURCE / "suburban" / "Models" / "OBJ format" / "building-type-t.obj",
    },
    {
        "id": "3",
        "name": "industrial_factory_b",
        "label": "Industrial Factory B",
        "obj": SOURCE / "industrial" / "Models" / "OBJ format" / "building-b.obj",
    },
]


def rewrite_line_prefix(lines: list[str], prefix: str, replacement: str) -> list[str]:
    replaced = []
    for line in lines:
        if line.startswith(prefix):
            replaced.append(replacement)
        else:
            replaced.append(line)
    return replaced


def prepare_one(spec: dict[str, str | Path]) -> tuple[str, Path]:
    src_obj = Path(spec["obj"])
    if not src_obj.exists():
        raise FileNotFoundError(f"Missing source OBJ: {src_obj}")

    src_mtl = src_obj.with_suffix(".mtl")
    if not src_mtl.exists():
        raise FileNotFoundError(f"Missing source MTL: {src_mtl}")

    src_texture = src_obj.parent / "Textures" / "colormap.png"
    if not src_texture.exists():
        raise FileNotFoundError(f"Missing source texture: {src_texture}")

    dst_dir = CHOICES / str(spec["name"])
    dst_textures = dst_dir / "Textures"
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst_textures.mkdir(parents=True, exist_ok=True)

    dst_obj = dst_dir / src_obj.name
    dst_mtl = dst_dir / src_mtl.name
    dst_ppm = dst_textures / "colormap.ppm"

    shutil.copy2(src_obj, dst_obj)
    shutil.copy2(src_mtl, dst_mtl)

    image = Image.open(src_texture).convert("RGB")
    image = image.transpose(Image.FLIP_TOP_BOTTOM)
    image.save(dst_ppm, format="PPM")

    obj_lines = dst_obj.read_text(encoding="utf-8", errors="ignore").splitlines()
    obj_lines = rewrite_line_prefix(obj_lines, "mtllib ", f"mtllib {dst_mtl.resolve()}")
    dst_obj.write_text("\n".join(obj_lines) + "\n", encoding="utf-8")

    mtl_lines = dst_mtl.read_text(encoding="utf-8", errors="ignore").splitlines()
    mtl_lines = rewrite_line_prefix(mtl_lines, "map_Kd ", f"map_Kd {dst_ppm.resolve()}")
    dst_mtl.write_text("\n".join(mtl_lines) + "\n", encoding="utf-8")

    return str(spec["id"]), dst_obj.resolve()


def main() -> None:
    prepared: list[tuple[str, Path, str]] = []
    for spec in SAMPLE_SPECS:
        sample_id, obj_path = prepare_one(spec)
        prepared.append((sample_id, obj_path, str(spec["label"])))

    print("Prepared OBJ samples:")
    for sample_id, obj_path, label in prepared:
        print(f"  {sample_id}: {label} -> {obj_path}")


if __name__ == "__main__":
    main()
