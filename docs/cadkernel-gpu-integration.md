# Dynedra GPU integration

This earlier integration check was not rerun for the current CPU benchmark.

The shaded Dynedra Lab path was checked separately after native mesh support was
added. Its hidden-window fixture creates a real Vulkan context, loads the font,
constructs and tessellates all eight solids, and presents the six original
scenario frames plus eight shaded solid frames. O0 compiled in 288.984 seconds
and ran in 7.953 seconds; O2 compiled in 308.735 seconds and ran in 5.609
seconds. Both runs required drawable frames and nonempty complete solid meshes.

These are integration times, including device and window startup, CPU geometry,
tessellation and presentation. They are not GPU-only timings and are not used
as a comparison with Rust. The native renderer currently sends each solid as
one triangle batch through the runtime's persistently mapped per-frame upload
buffer, with Vulkan depth testing and interpolated vertex color. A fair language
comparison still needs an equivalent Rust renderer, identical shaders and mesh
buffers, GPU timestamp queries, warm-up frames and the same device selection.

The planned compute path and correctness checks are in
[GPU geometry path](cadkernel-gpu-geometry.md).
