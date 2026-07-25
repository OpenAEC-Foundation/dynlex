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

## Volumetric geometry

`nano-choreography.dl` projects a deterministic point cloud generated from
Blender's CC0 Human Base Meshes bundle. Download the pinned source identified
in `generate-point-cloud.py`, verify its SHA-256, then run:

```bash
blender --background --python tools/homepage-shaders/generate-point-cloud.py -- \
  --blend /path/to/human_base_meshes_bundle.blend \
  --output web/shaders/geometry/vitruvian-points.f32 \
  --metadata web/shaders/geometry/vitruvian-points.json
./scripts/generate_homepage_shaders.sh
```

The geometry file stores one `float32x4` record for each vertex of every point
micro-triangle. Its metadata records the source URL, license, generator seed,
counts, and source digest. The manifest hashes both compiled shader stages and
the generated geometry, so editing either input updates the runtime record
without maintaining a separate cache key.
