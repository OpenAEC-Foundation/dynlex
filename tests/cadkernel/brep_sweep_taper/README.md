# Tapered sheet comparison

`verify.py` compares three native open-profile tapered sheets against the pinned
source kernel (`953d546b68aef4b6692566a1a9b077fc5bd9fb4f`). The profiles
are a line, a circular arc, and a connected chain whose second line is stored
backward. Both O0 and O2 runs compare 337 numeric fields per mode: body counts,
vertex positions, NURBS degrees, knots, control points and weights, plus face
and coedge orientation and parameter-space line curves. The native outputs
must also match exactly across optimization modes.

The verifier checks the source revision and file hash. It requires debug and
release reference libraries built from that checkout with the `brep` and
`offset` features under `build/cadkernel-upstream-offset-reference`. Run it
from the DynLex repository root:

```powershell
& 'C:/Users/rickd/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -B tests/cadkernel/brep_sweep_taper/verify.py --source 'C:/Users/rickd/.cargo/git/checkouts/cadkernel-2d155b32ebfceb83/953d546'
```

The required `cadkernel_brep_sweep_taper` fixture separately checks closed
polygon and circular tapers, lateral sheets, refusal cases, and topology
validity. The comparison here covers the three open-profile cases only; it
does not establish complete `sweep.rs` parity.
