# CAD-kernel performance comparison

Measured on 2026-09-17T03:29:10+00:00 with 10000 solid constructions per process.

The workload cycles through a cuboid, cylinder, sphere, cone, hexagonal pyramid and hexagonal frustum. Every result is topology-validated, its Euler characteristic and arena sizes enter an integer checksum, and its worst vertex gap and spatial bounds enter a floating-point checksum.

| Mode | Implementation | Compile time | Runtime median | Runtime p95 | Solids/s |
| --- | --- | ---: | ---: | ---: | ---: |
| O0 | DynLex | 54.038 s | 0.901839 s | 1.094167 s | 11088.4 |
| O0 | Pinned Rust | 12.123 s | 0.610247 s | 0.906549 s | 16386.8 |
| O0 | DynLex / Rust runtime | - | **1.478x** | - | - |
| O2 | DynLex | 53.037 s | 0.191611 s | 0.202922 s | 52188.9 |
| O2 | Pinned Rust | 26.814 s | 0.066738 s | 0.070037 s | 149839.0 |
| O2 | DynLex / Rust runtime | - | **2.871x** | - | - |

Each executable had 3 warm-up runs and 11 measured runs. Measured runs alternated implementation order and include process startup. Both implementations produced matching checksums before and throughout timing.

Compilation is reported for context. DynLex compiles the benchmark and its imported kernel dependency graph in one command. The Rust total is a full feature-enabled kernel library build plus the small benchmark driver, so the compilation units differ and their times are not a like-for-like compiler score.

## Environment

- OS: `Windows-11-10.0.26200-SP0`
- Processor: `not reported by OS`
- DynLex compiler SHA256: `38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`
- Rust: `rustc 1.91.1 (ed61e7d7e 2025-11-07)`
- Pinned source revision: `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`

Reproduce from the repository root:

```powershell
python -B tests/cadkernel/performance/benchmark.py --source <pinned-source-path>
```

The full sample arrays and compile phase timings are recorded in `docs/cadkernel-performance.json`.

## Dynedra GPU integration

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
