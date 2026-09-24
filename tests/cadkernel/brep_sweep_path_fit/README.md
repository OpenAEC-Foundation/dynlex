# Cubic path-frame fitting and local regularity

`brep_sweep_path_fit.dl` ports the cubic frame Bézier evaluation, adaptive
`fit_patch` subdivision and local Jacobian check from the pinned
`src/brep/sweep_path.rs`. The regularity function takes profile points that
have already been sampled by its caller. It does not yet construct a B-rep.

The differential checks seven fit and regularity cases at O0 and O2 against
the pinned Rust source. It compares all patch controls and sampled results;
the source revision and file hash are checked before execution.

```powershell
python -B tests/cadkernel/brep_sweep_path_fit/verify.py --source PATH_TO_PINNED_KERNEL
```

The returned patch list is owned by the caller and must be released exactly
once with `release cad path sweep fit`, including a refused result. Profile
sampling, path closure, twist and scale, and body assembly remain outside this
slice.
