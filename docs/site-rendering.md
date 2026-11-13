# Farm rendering migration

Farm's geometry, materials, cameras and lighting are being moved to DynLex using
the pinned shared UI SDK. Connection to the website remains pending while the
world renderer and controller are completed. The separate rendering probes use
their own build directory.

`site/games/farm/lighting.dl` owns the procedural sky: its hemisphere gradient,
warm horizon and HDR sun preserve the original shader. Reflections use GGX
importance sampling and the shared unsigned Hammersley sequence. The zero
roughness level retains the original sky; rough levels average incident radiance
with positive normal/light weights. The sampling derivation is documented in the
dynlex-ui repository's `docs/scenes.md`.

`site/games/farm/shaders/environment.dl` renders one cube face and mip level. Its
float32x4 uniform at binding 0 contains width, height, face index and roughness;
binding 1 supplies a signed int32 sample count. Face order is +X, -X, +Y, -Y, +Z,
-Z. The output is linear HDR and requires a float render target. A fixed image can
be baked once; empty its draw list afterward with `load`/`store` attachments to
retain its pixels without further GPU passes. Configuration changes rebake it.

Verify the portable model with:

```bash
python3 tests/web/site_runtime.py --case site_farm_lighting
```

This checks native compilation at O0/O2 and both WebAssembly compiler paths,
including ownership. The sky is compared with fourteen recorded floating-point
pixels from the original GLSL, covering the hemispheres, horizon and sun.
`tests/web/references/farm_sky.frag` preserves that reference independently of the
application implementation. Normal tests never regenerate its expected values.

Verify actual shader output on both browser backends with:

```bash
python3 tests/web/prepare_farm_renderer.py --output .cache/farm-renderer-probe --browser
```

The command compiles the real Farm shader with the shipped browser compiler,
generates 480 reflection samples using the portable DynLex functions, and tests
the GPU pixels on WebGPU and WebGL. It uses an isolated browser profile on a
private Xvfb display with audio muted. The probe covers generated cube faces,
multiple roughness levels, HDR storage and original sky reference pixels.
