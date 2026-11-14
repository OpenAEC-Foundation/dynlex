# Homepage shaders

The homepage and Studio share the DynLex scene descriptions in
`site/shaders/catalog.dl`, the pass-graph linker in `site/shaders/program.dl`,
and the shared UI renderer. Build their WGSL/GLSL and packaged graph together:

```bash
./scripts/build_web_root.sh
```

The builder compiles the descriptions and shaders, links their typed interfaces,
and publishes one complete output directory. Deployment serves each release's
assets from its immutable versioned URL.

## Terrain geometry

`endless-terrain.dl` displaces camera-centered terrain and water surfaces in its
vertex stage. `site/shaders/terrain.dl` creates their deterministic `float32x4`
vertex buffer and fixed `uint32` triangle indices from the sampling rules in
`site/shaders/catalog.dl`. The configured row and column counts describe a reference
horizontal framebuffer width; the same generator scales both dimensions to the
viewer's actual horizontal pixel count and caches the current result on its
complete immutable configuration descriptor. Exponential distance and column
decay preserve proportional density toward the horizon. A generic index zipper
triangulates adjacent rows with different column counts, keeping every
transition watertight.

Indexing lets the GPU evaluate each displaced sample once instead of once per
triangle corner. The first geometry coordinate selects a fixed camera ray; the
second stores an exponentially spaced radial world distance. The vertex shader
derives each ray's forward distance from its angle, so outer ultrawide rays and
the center ray advance equally through the height field. Every column therefore
remains straight through perspective projection without under-sampling the
terrain at the sides. Increasingly distant rows still avoid spending equal work
on sub-pixel geometry. The coarser water grid receives analytic waves and
normals; depth testing against the terrain creates the shoreline without ray
marching. The buffer also embeds one full-screen sky triangle, allowing both
compiled stages and all three surfaces to share named interpolants in one draw.
The banner resolves density against the full hero width, so animating the
thought-cloud mask does not rebuild topology. The IDE uses the same generator
against its preview width. Changing the camera-distance range, either sampling
rule, or generator configuration creates
a distinct cache entry automatically.

## Volumetric geometry

`nano-choreography.dl` projects a deterministic density cloud generated from
[The Vitruvian Man by Fri](https://sketchfab.com/3d-models/the-vitruvian-man-6c0b99ce8463468fbd00f304dbe7e105),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Download the pinned archive through Sketchfab, extract `Vitruvian.stl`, verify
the hashes recorded in `generate-point-cloud.py`, then run:

```bash
blender --background --python tools/homepage-shaders/generate-point-cloud.py -- \
  --stl .cache/homepage-shaders/source/The_Vitruvian_Man/Vitruvian.stl \
  --output site/assets/shaders/geometry/vitruvian-points.f32 \
  --metadata site/assets/shaders/geometry/vitruvian-points.json
./scripts/build_web_root.sh
```

The geometry file stores one `float32x4` record for each vertex of every point
micro-triangle. Each axis uses the `paired-unorm12` encoding to carry both the
procedural motorcycle source coordinate and the model-derived Vitruvian target
coordinate. This lets the same point population form both shapes without a
second buffer or a runtime geometry generator. Recursive spatial bisection
pairs neighboring regions of both shapes, so coherent flocks travel together
instead of dissolving into a random cloud. The fourth coordinate combines the
micro-triangle corner with a wheel bit, allowing the vertex shader to rotate the
generated tires, hubs, and spokes without moving nearby frame points. Its
metadata records the encoding, pairing, creator,
license, source URL, source hashes, modifications, generator seed, and density
counts. The packaged render graph names the compiled stages and geometry assets; rebuilding
updates them together.
