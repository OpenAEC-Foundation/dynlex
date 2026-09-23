# Solid revolution region differential

`verify.py` compares the native DynLex implementation with `src/brep/sweep.rs::revolve_region` at pinned cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. It verifies the source file hash, builds both reference and native probes at O0 and O2, and compares validity, every arena count, root/shell structure, validation results, pcurve count and vertex coordinates.

The fifteen cases include a single closed contour, full revolutions with one or two void shells, half revolutions cut by one or two inner contours, a negative half turn, separate voids, invalid input, an axis-touching profile, and partial torus and sphere profiles. The partial cases also exercise the newly ported capped solid revolution.

Run from the DynLex repository root with the pinned reference libraries built using `brep` and `offset`:

```text
python tests/cadkernel/brep_revolve_region/verify.py --source <pinned-cadkernel-checkout>
```

The summary and binaries are written under `build/`.
