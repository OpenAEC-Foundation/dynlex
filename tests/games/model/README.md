# City FBX Viewer

This folder contains a small-town FBX asset and a DynLex viewer.

## Quick start

```bash
./scripts/build.sh --lint=false && cmake --build build --target fbx_to_obj && ./build/fbx_to_obj tests/games/model/assets/source/city.fbx tests/games/model/assets/generated/city.obj --triangle-step 1 && ./build/dynlex tests/games/model/viewer.dl -o tests/games/model/viewer && ./tests/games/model/viewer
```

## Controls

- `W` / `S`: move forward/backward
- `A` / `D`: move right/left
- `Q` / `E`: move down/up
- `Left` / `Right`: yaw left/right
- `1`: textured/material mode
- `2`: UV debug mode
- `3`: vertex-color mode

## Notes

- The FBX import step currently converts to OBJ via `build/fbx_to_obj`.
- Use `--triangle-step N` with `N > 1` only when you explicitly want decimation.

## Texture Override Mapping

When FBX materials do not include complete texture bindings, `fbx_to_obj` now supports an override file:

- Path: `tests/games/model/assets/source/material_texture_overrides.txt`
- Format: `MaterialName = texture_file`
- Texture file can be a filename, relative path, or absolute path.

Example:

```text
# material -> texture override
default = White_Tiles.jpeg
Colore = lowpoly_tex-2.png
Texture_Strada = Texture_Strada.png
```

## Downloaded Free OBJ Samples

Three free OBJ samples are downloaded from OpenGameArt (Kenney city kits) and staged for preview:

- Source pages:
  - https://opengameart.org/content/city-kit-commercial
  - https://opengameart.org/content/city-kit-suburban
  - https://opengameart.org/content/city-kit-industrial

- Prepare/update staged samples:

```bash
python3 tests/games/model/prepare_obj_samples.py
```

- Launch one sample in the viewer:

```bash
tests/games/model/run_obj_samples.sh 1
tests/games/model/run_obj_samples.sh 2
tests/games/model/run_obj_samples.sh 3
```
