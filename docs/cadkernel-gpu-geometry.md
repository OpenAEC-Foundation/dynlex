# GPU geometry path

## Current boundary

The graphics runtime draws triangles with Vulkan. B-rep construction, Boolean
operations, constrained UV triangulation and surface-to-3D mesh evaluation still
run on the CPU. The existing constructor benchmark does not measure meshing or
GPU execution. `tests/cadkernel/gpu/capability.dl` reports the selected graphics
device and whether its graphics queue can also execute compute shaders with
64-bit floating-point arithmetic. A true result means the logical device has
the Vulkan `shaderFloat64` feature enabled; it does not mean a compute pipeline
or GPU mesher exists yet.

The reference mesher triangulates each face in its parameter plane, constrains
topological boundaries and holes, then maps vertices to the analytic surface.
That order preserves trims and winding. The reference Boolean implementation
rejects intersections it cannot resolve exactly rather than returning a
plausible but invalid solid. GPU floating-point arithmetic by itself cannot
turn a triangle approximation into an exact surface or replace the topology
checks needed for that contract.

## Implementation sequence

1. Measure construction, UV triangulation, surface evaluation, mesh assembly,
   upload and drawing separately on fixed solids. Record CPU and GPU elapsed
   times, mesh sizes and device choice. Use GPU timestamp queries for dispatch
   and drawing, and wall time for transfer plus synchronization.
2. Add a DynLex compute shader stage and Vulkan storage buffers, device-local
   mesh and index buffers, dispatch, barriers and feature-gated 64-bit arithmetic.
   Keep the current CPU path when the selected device lacks the feature.
3. Keep trim loops, holes, periodic seams, adaptive subdivision decisions and
   triangle connectivity on the CPU. Batch independent UV-to-surface position
   and normal evaluations on the GPU, first for analytic faces and then for
   NURBS spans. Avoid copying results back when they are used only for drawing;
   retain a CPU result for exports and exact geometry validation.
4. Evaluate GPU acceleration of independent face-pair candidates for Boolean
   operations only after the mesher is correct and measured. The CPU remains
   authoritative for intersection classification, topology edits and final
   solid validation. A GPU candidate must never silently change a valid/invalid
   outcome.

## Acceptance checks

- Compare GPU output with the CPU kernel on planar, curved, trimmed and NURBS
  faces, including holes, poles, periodic seams, fine tolerances and large
  world coordinates. Check positions, normals, winding, boundary continuity,
  triangle counts where the schedule is deterministic, and complete-solid
  validation. Use a documented numerical tolerance for display meshes.
- Verify both feature branches: a 64-bit compute-capable device and a device
  without `shaderFloat64`. Unsupported shapes and devices use the CPU path.
- Report end-to-end mesh latency including dispatch and transfers. Compare a
  reused static mesh separately from a newly tessellated solid. A graphics
  frame timing alone cannot establish a geometry speedup.

On the local development machine the capability probe reports `true` on the
selected discrete device and `false` when the integrated driver is selected.
The GPU geometry implementation and its performance comparison remain open.
