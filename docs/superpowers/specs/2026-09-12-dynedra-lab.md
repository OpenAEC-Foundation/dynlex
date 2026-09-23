# Dynedra Lab design

A native DynLex validation application for inspecting the geometry produced by
the port. It uses the existing graphics/font runtime and the same kernel modules
as the automated fixtures.

## Visual system

- Canvas: cool white `#F7F9FC`; controls: white `#FFFFFF`.
- Text: dark blue `#233046`; secondary text/grid: `#6B7B93` / `#DDE4EF`.
- Computed geometry: cobalt `#245BC7`; editable controls: ochre `#C26D26`.
- Typography: the repository's licensed Liberation Mono asset, 30 px title,
  18 px section names, 14 px controls/measurements. Its fixed columns support
  coordinate and parameter inspection; avoid decorative labels and badges.
- Layout: a narrow left control rail and one large model viewport. The geometry,
  its control net and world axes share the viewport. Keep all controls left aligned.
- Input: visible clickable controls plus keyboard equivalents, with continuous
  orbit/zoom and discrete operation/parameter changes. Rebuild geometry only when
  its inputs change. No idle camera animation.
- Measurements describe their meaning and units. Sampled quantities are marked
  approximate. Invalid geometry displays the actual refusal and retains enough
  input context to diagnose it.

```
Dynedra Lab                          Operation name
----------------------------------------------------
Operation / parameters |                            |
                       |     Computed geometry      |
Measurements           |     and control net        |
                       |                            |
----------------------------------------------------
Input hint / geometry status
```

The viewport is the visual focus. Keep the rail plain and compact; no repeated
metric cards, marketing copy or invented pass indicators. Curve, surface and
solid cases become available only when their native modules are implemented.

## Implementation and verification

Separate kernel scenario construction, application state/input transitions and
drawing. Tests import the construction/state modules, change the same parameters
and verify regenerated results. The interactive entry point contains no embedded
test sequence. A separate finite-frame graphics probe checks initialization,
font loading and draw completion. Native rendering uses binary32 presentation
coordinates only after binary64 geometry calculation and projection.

Initial scenarios exercise rational curves, Bezier controls, helix sampling and
NURBS surfaces. Extend the same application with planar operations and B-rep
fixtures as those modules complete. Initial scenarios do not certify the full
83-file kernel.
