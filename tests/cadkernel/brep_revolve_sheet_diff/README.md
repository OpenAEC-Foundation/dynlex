# Sheet revolution differential

`verify.py` compares the pinned Rust `revolve_surface` and
`revolve_surface_region` operations with the DynLex sheet equivalents.
Both probes receive identical numeric profiles, pivot, axis, and angle.
Each successful result is checked for validity, all nine B-rep arena counts,
root and shell/face grouping, validation flaws, analytic surface-kind counts,
boundary and two-coedge edge counts, pcurve count, and every vertex position.
Invalid inputs must be refused by both implementations.

Vertex arena insertion order is not a geometric contract. The verifier keeps
duplicate vertices, sorts the resulting coordinate triples, and compares them
with an absolute tolerance of `1e-8` and a relative tolerance of `1e-10`.
Counts and topology grouping remain exact. Rust and DynLex are each compiled
and checked at O0 and O2; binaries run through the shared
`tests/cadkernel/verify.py::run_process` helper.

The case set covers open and closed line chains, partial, full, near-full,
reflex and negative turns, analytic plane/cone/cylinder/torus/sphere sheets,
axis contacts and separated components, two- and three-profile sheet regions,
and invalid profiles, axes, pivots, and angles. Ellipse and NURBS profile
segments are outside this harness because the DynLex sheet operation currently
refuses them while the pinned Rust operation supports them.

The verifier checks the exact source revision and SHA-256 of
`src/brep/sweep.rs`. It requires the pinned debug and release
`libcadkernel.rlib` builds in `build/cadkernel-upstream-offset-reference`,
as well as `build/dynlex.exe`.

The pinned run passes 35 cases at O0 and O2, with 1,722 Rust–DynLex field
comparisons. This checks the supported line and circular-arc profile subset;
it does not establish parity for ellipses or NURBS.

From the DynLex repository root:

```text
python tests/cadkernel/brep_revolve_sheet_diff/verify.py --source <pinned-cadkernel-checkout>
```

Use `--case NAME` to select case names containing `NAME`. Probe executables
are built in a temporary directory and removed after the run.
