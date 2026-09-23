# 3D polyline sweep differential

`verify.py` compares the native DynLex sweep to `src/brep/sweep.rs::sweep_along_polyline3d` at pinned cadkernel revision `953d546b68aef4b6692566a1a9b077fc5bd9fb4f`. The source file hash is checked before building both reference and native probes at O0 and O2. Nine cases cover straight, translated, collinear, right-angle and sloped paths, as well as reversed, stationary, short and open-profile refusals. For valid bodies the comparison includes every arena count, root count, validation result and vertex coordinate.

Run from the DynLex repository root after building the reference libraries with the `brep` and `offset` features:

```text
python tests/cadkernel/brep_sweep_polyline3d/verify.py --source <pinned-cadkernel-checkout>
```

The script writes its summary and binaries under `build/`.
