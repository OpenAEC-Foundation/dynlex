# Homepage shaders

The banner renders DynLex shaders directly in WebGL2. `generate.mjs` compiles
every configured fragment stage and optional vertex stage with the browser
compiler, writes deterministic GLSL, caches compiler-produced semantic tokens,
and creates the runtime manifest.

```bash
./scripts/generate_homepage_shaders.sh
./scripts/generate_homepage_shaders.sh --check
```

Changing a configured shader or any transitive DynLex import changes the
manifest input hash. The generated GLSL and semantic-token cache are replaced
atomically, so the banner never mixes source and artifacts from different
revisions.

## Terrain geometry

`endless-terrain.dl` displaces a fixed camera-centered grid in its vertex
stage. `generate-terrain-grid.mjs` creates the deterministic `float32x4`
vertex buffer and `uint16` triangle indices from the configured row and column
counts; changing either count regenerates both geometry hashes automatically.
Indexing lets the GPU evaluate each displaced grid point once instead of six
times per cell. The grid uses quadratic depth spacing to spend most vertices
near the camera and embeds one full-screen sky triangle, allowing the compiled
vertex and fragment stages to share named interpolants in a single depth-tested
draw.

## Volumetric geometry

`nano-choreography.dl` projects a deterministic density cloud generated from
[The Vitruvian Man by Fri](https://sketchfab.com/3d-models/the-vitruvian-man-6c0b99ce8463468fbd00f304dbe7e105),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Download the pinned archive through Sketchfab, extract `Vitruvian.stl`, verify
the hashes recorded in `generate-point-cloud.py`, then run:

```bash
blender --background --python tools/homepage-shaders/generate-point-cloud.py -- \
  --stl .cache/homepage-shaders/source/The_Vitruvian_Man/Vitruvian.stl \
  --output web/shaders/geometry/vitruvian-points.f32 \
  --metadata web/shaders/geometry/vitruvian-points.json
./scripts/generate_homepage_shaders.sh
```

The geometry file stores one `float32x4` record for each vertex of every point
micro-triangle. Each axis uses the `paired-unorm12` encoding to carry both the
procedural motorcycle source coordinate and the model-derived Vitruvian target
coordinate. This lets the same point population form both shapes without a
second buffer or a runtime geometry generator. Recursive spatial bisection
pairs neighboring regions of both shapes, so coherent flocks travel together
instead of dissolving into a random cloud. Its metadata and the runtime manifest
record the encoding, pairing, creator, license, source URL, source hashes,
modifications, generator seed, and density counts. The manifest hashes both
compiled shader stages and the generated geometry, so editing either input
updates the runtime record without maintaining a separate cache key.
