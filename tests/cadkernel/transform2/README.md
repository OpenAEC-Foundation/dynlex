# Affine transformations in the plane

Native implementation: `lib/cadkernel/transform2.dl`, from the complete
`src/geom2d/transform.rs` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f` (340 production lines, 18 tests).

`cad transform2` stores two axes and an origin. Identity/default, translation,
rotation, per-axis scale, insertion, composition, point/vector application,
determinant, similarity detection, equality and cloning preserve source order
and thresholds. A point includes translation; a direction does not.

`the cad transformed curve by map` returns `valid` and a managed `value`.
Lines, rays and construction lines follow arbitrary maps. Similarities retain
circles and circular arcs; nonuniform maps promote them to ellipses. Principal
axes and mapped endpoint parameters retain the source's axis-order and reflection
rules, including full-period handling. NURBS rebuild from transformed controls
with their knots and weights. Polyline bulges change sign under reflection;
a nonuniform map rejects bulges with magnitude greater than `1e-12`, as upstream.
No additional finite-value validation is imposed on source-accepted inputs.

Input geometry is borrowed. Output NURBS lists and polyline vertices have
independent storage. Ordinary result copies retain their managed data; failure
results remain safe to copy/reset. Scalar transform records copy by value.
`brep_debug.dl` supplies the optional structural debug formatter.

## Verification

All 18 original test groups are translated in `main.dl` without weakening their
assertions. `transform2_ownership` checks default/clone/equality, independent
control/weight/knot/vertex buffers, returned-local lifetimes and safe failures.
Both fixtures pass at O0/O2.

The differential runner compiles the unchanged pinned Rust module and executes
all 18 source tests at O0/O2. It compares all active output fields and sampled
points/tangents for 354 cases: similarities, affine shear, reflection, singular
maps, threshold neighbors, short chains, extreme magnitudes, NaN/infinity,
periodic arcs and deterministic random inputs. Result: **24,230 Rust scalar
comparisons**, with exact optimization parity independently in Rust and DynLex.
Integer/flag fields are exact. Floating values use relative tolerance `3e-12`;
coordinate cancellation has a 16-ULP bound at the transformed geometry scale.
Signed-zero agreement and non-finite classification remain explicit checks.

Compiler SHA-256:
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/transform2-checks/summary.json`,
`build/transform2-differential.log`, `build/transform2-ownership.log`.

```powershell
python tests/cadkernel/transform2/verify.py --source <pinned-checkout>
```

On Windows the reference uses MSVC rustc and the native C math runtime.
Override `--rustc` for an equivalent local toolchain. The runner rejects source
drift and reports its compiler and source hashes; it never updates expectations.
