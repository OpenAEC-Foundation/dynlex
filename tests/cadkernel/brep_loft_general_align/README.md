# General loft profile alignment

This independent module ports the profile alignment stage of pinned
`src/brep/loft_general.rs::prepared()` at revision
`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It uses the existing
section-wire builder, determines transport normals along the section series,
and selects each later wire's traversal direction and existing span-start
anchor by the source's 24-sample cost. Closed wires can move their periodic
seam without changing their exact homogeneous Bézier geometry. Hole wires
align independently to the corresponding wire of the preceding section.

The differential driver checks the pinned source revision and relevant file
hashes, compiles the source reference and DynLex probe at O0 and O2, and
compares status, section frames and centres, wire flags, span intervals, and
all homogeneous controls. The required fixture covers seam and winding
selection, alignment disabled, refusals, and 200 result-retention iterations
with nested span data after input release at both optimization levels.

Run from the repository root:

```text
python -B tests/cadkernel/brep_loft_general_align/verify.py --source <pinned-source-checkout>
python -B tests/cadkernel/verify.py --filter cadkernel_brep_loft_general_align
```

The API accepts profile sections only. It does not represent first/last
point sections, custom normal or draft settings, guide curves, compatibility
subdivision, loft patches, caps, or a public B-rep body. Its refusal codes
describe the supported profile subset and cannot cover point-section errors.
