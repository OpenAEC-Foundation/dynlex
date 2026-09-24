# Path sweep transport

`lib/cadkernel/brep_sweep_path_transport.dl` ports the frame transport,
minimal rotation, mitre and twist/scale helpers in the pinned
`src/brep/sweep_path.rs`. It is a foundation for the still incomplete public
spatial-path sweep and does not build a swept body by itself. Input path
tangents are expected to be normalized, as they are in the source caller.

The required fixture checks parallel, antiparallel and quarter-turn transport,
frame translation, a right-angle mitre, and twist. The differential runner
compares 11 cases / 56 scalar fields with an exact excerpt of the pinned Rust
helper formulas at O0 and O2, and requires native optimization parity. The
cases cover oblique tangents, a near-antiparallel branch, opposite mitre
refusal, and a degenerate twist frame. The reference source hash and revision
are checked before the comparison.

```powershell
python tests/cadkernel/brep_sweep_path_transport/verify.py --source PATH_TO_PINNED_SOURCE
```
