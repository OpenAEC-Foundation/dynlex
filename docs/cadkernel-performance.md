# CAD-kernel performance comparison

Measured on 2026-09-24T04:56:36+00:00 with 10000 solid constructions per process.

The workload cycles through a cuboid, cylinder, sphere, cone, hexagonal pyramid and hexagonal frustum. Every result is topology-validated, its Euler characteristic and arena sizes enter an integer checksum, and its worst vertex gap and spatial bounds enter a floating-point checksum.

This is an interim constructor workload. It does not cover unfinished kernel operations or measure GPU rendering speed.

| Mode | Implementation | Compile time | Runtime median | Runtime p95 | Solids/s |
| --- | --- | ---: | ---: | ---: | ---: |
| O0 | DynLex | 68.955 s | 0.758593 s | 1.021420 s | 13182.3 |
| O0 | Pinned Rust | 8.294 s | 0.486105 s | 0.520549 s | 20571.7 |
| O0 | DynLex / Rust runtime | - | **1.561x** | - | - |
| O2 | DynLex | 73.104 s | 0.162281 s | 0.174606 s | 61621.4 |
| O2 | Pinned Rust | 19.072 s | 0.056591 s | 0.064466 s | 176705.9 |
| O2 | DynLex / Rust runtime | - | **2.868x** | - | - |

Each executable had 3 warm-up runs and 11 measured runs. Measured runs alternated implementation order and include process startup. Both implementations produced matching checksums before and throughout timing.

Compilation is reported for context. DynLex compiles the benchmark and its imported kernel dependency graph in one command. The Rust total is a full feature-enabled kernel library build plus the small benchmark driver, so the compilation units differ and their times are not a like-for-like compiler score.

## Environment

- OS: `Windows-11-10.0.26200-SP0`
- Processor: `not reported by OS`
- DynLex compiler SHA256: `8bf3737efb24d218e2949c463f5ea102fe26202f5053fb79105104864bad3f61`
- Rust: `rustc 1.91.1 (ed61e7d7e 2025-11-07)`
- Pinned source revision: `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`

Reproduce from the repository root:

```powershell
python -B tests/cadkernel/performance/benchmark.py --source <pinned-source-path>
```

The full sample arrays and compile phase timings are recorded in `docs/cadkernel-performance.json`.
Dynedra rendering integration is documented separately in [the GPU integration note](cadkernel-gpu-integration.md).
