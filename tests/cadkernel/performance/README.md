# CAD-kernel performance workload

This directory compares the native DynLex port with the pinned source crate on
one deterministic B-rep workload. It cycles through six solid constructors and
then performs topology validation, Euler and arena accounting, vertex-gap
measurement and spatial bounds. Both executables emit one integer checksum and
one floating-point checksum, which the runner compares before and during timing.

Run from the repository root:

```powershell
python -B tests/cadkernel/performance/benchmark.py --source C:/path/to/pinned/cadkernel
```

Defaults are three warm-up runs followed by eleven alternating measured runs at
both O0 and O2, with 10,000 solids built per process. Wall time includes process
startup. The runner writes the raw samples to `docs/cadkernel-performance.json`
and the readable result to `docs/cadkernel-performance.md`.

The runtime comparison uses the same inputs, operations and checksum in both
implementations. Compilation timings are contextual: DynLex compiles the
benchmark's imported dependency graph, while the Rust command builds the full
feature-enabled kernel library and then links the benchmark driver.
