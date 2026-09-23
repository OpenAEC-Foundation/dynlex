# B-rep blend verification

This directory verifies the native public blend dispatcher against cadkernel
revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`.

The current native implementation covers public validation, slot-ordered
deduplication, single-edge convenience calls, exact circular and prismatic
dispatch, closed convex planar halfspace chamfers, open planar sheet chamfers,
and supported planar fillets. The general reconstruction follows the source
formulas for edge frames, unequal setbacks, relative-coordinate triple-plane
intersections, collinear cleanup, cylindrical fillet supports, exact circular
and elliptical boundary curves, and spherical three-edge corner patches.
Existing cylindrical and spherical blends are recognized, cut into subsequent
operations, and restored atomically. Open sheets are closed temporarily by
boundary halfspaces; only source supports and new cut faces are copied back,
with shared vertices and edges stitched within the operation tolerance. Inputs
are borrowed and successful results own independent reconstructed bodies.

`main.dl` checks public behavior on boxes, circular solids, a wedge, a
pyramid, a polygonal frustum and an L-shaped open sheet. It also checks
single-edge calls, duplicate selections, source preservation, invalid input,
an oversized chamfer, a supported wedge fillet, an unsupported fillet end
condition, repeated blends, multi-edge elliptical seams and spherical corner
restoration. The required fixture runs this file at both optimization levels.

`verify.py` builds `probe.dl` and `reference.rs` at O0 and O2. It compares
error categories and edge context for rejected operations. For successful
operations it compares vertex, edge and face counts, Euler characteristic,
topology flaws, worst vertex gap, complete vertex sets, curve-kind histograms
and surface-kind histograms. The case set covers wedge, pyramid and frustum
families with 3, 4, 5, 8 and 17 sides; open-sheet shared and boundary edges;
asymmetric and excessive distances; reordered, duplicate and multi-edge
selections; repeated fillet/chamfer operations; varied scale and world
coordinates around `1e9`; and supported and rejected fillet radii.

The recorded full run passes 748 cases and 34,182 comparisons across O0 and
O2, with 267 valid results in each mode. The compiler SHA-256 was
`38ebb889d576c9d9343f3a0cbbfa7025be611cef65a010e23662169b7457fc5a`.
The final build measured 147.859 seconds for native O0 and 59.375 seconds for
native O2; the linked Rust probes took 0.313 and 0.656 seconds. The recorded
manifest is `build/brep-blend-with-presspull-final/summary.json`.

Run the differential check with:

```powershell
python -B tests/cadkernel/brep_blend/verify.py `
  --source C:/path/to/cadkernel-at-953d546 `
  --compiler build/dynlex.exe `
  --output build/brep-blend-checks
```

The public single-edge overloads and press-pull forwarding are exercised by
the required fixture at O0 and O2. The complete blend differential above was
rebuilt after linking the native press-pull module.
