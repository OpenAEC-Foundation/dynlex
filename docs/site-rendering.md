# Farm rendering migration

Farm's geometry, materials, cameras, lighting and website controls are written
in DynLex using the pinned shared UI SDK. The challenge opens from the homepage,
renders its world, accepts pointer and keyboard input, and compiles roles through
the same compiler service as the standard editor. The separate rendering probes
use their own build directory.

`interface.dl` declares the controls. `controller.dl`, `session.dl` and the focused
control modules connect them to the portable simulation and account progress.
The browser host supplies graphics, storage and worker services. Leaving the
challenge cancels pending role compilation and pauses its clock.

The landscape now renders through `site/games/farm/program.dl`, a DynLex render
graph using the shared scene ABI. The landscape alone groups 617 initial mesh
instances into 14 draw batches. Characters share that geometry bank and submission
path; the initial player, worker and chickens bring the scene to 693 instances in
15 batches. The graph bakes a 128-pixel, eight-level HDR reflection cube and a DFG
lookup, renders directional shadows into a 2048-pixel depth map, draws the scene
with four-sample antialiasing, and applies the shared filmic tone mapping. All
stages are DynLex source under `site/games/farm/shaders/`. Browser hosts only
execute the graph and uploads; they contain no Farm scene or lighting logic.

`render_inputs.dl` supplies the camera, light, weather and clock values. Sunlight
uses the same shared view basis as perspective cameras, with the core
orthographic projection. Disable baked passes after their first render and
disable the shadow pass between world changes; this preserves the stored images
without executing another clear or draw.

`scene_characters.dl` synchronizes player, workers, cow and chickens with the
simulation. The shared motion library queues visual waypoints, accelerates,
brakes and turns using elapsed time. Local character poses supply walking legs,
body lift, chopping arms, animal tails and pecking heads. Their named pivots and
world bounds remain the same ones used for rendering and picking.

Equipment composes the shared item models beneath each hand, offset by the item's
named grip. Inventory changes preserve the existing body poses and unchanged
inventories retain their model storage. Departed entities release their scene
objects and pending motion. The character fixture tests native O0/O2 and both
WebAssembly frontends, including zero retained allocations.

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
It also compares 62 independently recorded coat, water-normal and fog cases,
renders the complete landscape through the public graphics service, checks that
cached passes retain exactly the same image, and compares both backends' pixels.
The scene data generator releases its scene, graph and serialization allocations.

The sky readers preserve their inputs. DynLex parameters refer to caller storage;
normalizing a parameter in place would also change a later material's position.
The water vertex stage uses each instance's full world position for a continuous
wave phase, then transforms its perturbed object-space normal through the shared
inverse transpose. This corrects the old InstancedMesh shader patch's omission of
the instance transformation.
